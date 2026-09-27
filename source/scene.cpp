#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
extern "C"
{
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <common.hpp>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>

#include "okara_gltf_importer.h"

struct Vertex
{
    glm::vec3 position;
    glm::vec2 tex_coord;
};

struct LoadedTexture
{
    GLuint id = 0;
    GLuint64 handle = 0;
};

struct DrawElementsIndirectCommand
{
    GLuint count;         // number of indices to draw
    GLuint instanceCount; // set to 1 (or N for instancing)
    GLuint firstIndex;    // byte offset in EBO divided by sizeof(uint32_t)
    GLint baseVertex;     // offset added to vertex index before indexing VBO
    GLuint baseInstance;  // offset added to gl_InstanceID / gl_DrawID
};

struct MaterialData
{
    GLuint64 base_col_handle;
};

struct PerFrameData
{
    glm::mat4 model_view_proj;
};

// helper to create a fallback 1x1 solid texture for missing textures
LoadedTexture CreateFallbackTexture()
{
    GLuint id;
    glCreateTextures(GL_TEXTURE_2D, 1, &id);
    glTextureStorage2D(id, 1, GL_RGBA8, 1, 1);
    uint32_t pixel = 0xFFFFFFFF; // White fallback
    glTextureSubImage2D(id, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &pixel);

    GLuint64 handle = glGetTextureHandleARB(id);
    glMakeTextureHandleResidentARB(handle);
    return {id, handle};
}

// Global buffers
GLuint vao, vbo, ebo;
GLuint indirect_buffer;
GLuint material_ssbo;

std::vector<DrawElementsIndirectCommand> indirect_commands;
std::vector<MaterialData> materials_gpu;

void SetupBatchRendering(const okara::gltf::Loader &loader, const std::vector<LoadedTexture> &gpu_textures, const LoadedTexture& default_tex)
{
    std::vector<Vertex> global_vertices;
    std::vector<uint32_t> global_indices;

    uint32_t current_first_index = 0;
    int32_t current_base_vertex = 0;
    uint32_t draw_id = 0;

    for (const okara::gltf::Mesh &mesh : loader.meshes_)
    {
        for (const auto &p : mesh.primitives)
        {
            // pack vertices
            for (const auto &vert : p.vertices)
            {
                global_vertices.push_back({{vert.position.x, vert.position.y, vert.position.z}, {vert.uv.x, vert.uv.y}});
            }
            // pack indices
            global_indices.insert(global_indices.end(), p.indices.begin(), p.indices.end());
            // resolve material
            GLuint64 tex_handle = default_tex.handle; // default handle
            if (p.material < loader.materials_.size())
            {
                int tex_idx = loader.materials_[p.material].base_color_texture;
                if (tex_idx >= 0 && tex_idx < gpu_textures.size())
                {
                    tex_handle = gpu_textures[tex_idx].handle;
                }
            }
            materials_gpu.push_back({tex_handle});

            // build indirect command
            DrawElementsIndirectCommand cmd{};
            cmd.count = static_cast<GLuint>(p.indices.size());
            cmd.instanceCount = 1;
            cmd.firstIndex = current_first_index;
            cmd.baseVertex = current_base_vertex;
            cmd.baseInstance = draw_id;

            indirect_commands.push_back(cmd);

            // advance offsets
            current_first_index += static_cast<uint32_t>(p.indices.size());
            current_base_vertex += static_cast<int32_t>(p.vertices.size());
            ++draw_id;
        }
    }

    // create single global VAO, VBO, EBO
    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, global_vertices.size() * sizeof(Vertex), global_vertices.data(), 0);

    glCreateBuffers(1, &ebo);
    glNamedBufferStorage(ebo, global_indices.size() * sizeof(uint32_t), global_indices.data(), 0);

    glCreateVertexArrays(1, &vao);
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(vao, 0, 0);

    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, tex_coord));
    glVertexArrayAttribBinding(vao, 1, 0);

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao, ebo);

    // create Indirect Command Buffer
    glCreateBuffers(1, &indirect_buffer);
    glNamedBufferStorage(indirect_buffer, indirect_commands.size() * sizeof(DrawElementsIndirectCommand), indirect_commands.data(), 0);

    // create Material SSBO
    glCreateBuffers(1, &material_ssbo);
    glNamedBufferStorage(material_ssbo, materials_gpu.size() * sizeof(MaterialData), materials_gpu.data(), 0);
}

int main()
{
    WindowManager::Window window{};
    window.Create("Scene", 800, 600);
    window.EnableOutputDebug();

    glEnable(GL_DEPTH_TEST);

    // default fallback texture if a material lacks a texture
    LoadedTexture default_texture = CreateFallbackTexture();

    okara::gltf::Loader loader{};
    loader.load("../../../resources/models/heather.glb");

    // load textures
    std::vector<LoadedTexture> gpu_textures;
    gpu_textures.reserve(loader.textures_.size());

    for (const auto &tex : loader.textures_)
    {
        if (tex.source < 0 || tex.source >= loader.images_.size())
        {
            gpu_textures.push_back(CreateFallbackTexture());
            continue;
        }

        const okara::gltf::Image &src_img = loader.images_[tex.source];
        int w, h, c;
        unsigned char *pixels = stbi_load_from_memory(reinterpret_cast<const unsigned char *>(src_img.data.data()), static_cast<int>(src_img.data.size()), &w, &h, &c, 4);
        if (!pixels)
        {
            std::cerr << "Failed to load image data for texture, using fallback.\n";
            gpu_textures.push_back(CreateFallbackTexture());
            continue;
        }

        GLuint tex_id;
        GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(w, h)))) + 1;

        glCreateTextures(GL_TEXTURE_2D, 1, &tex_id);
        glTextureStorage2D(tex_id, levels, GL_RGBA8, w, h);
        glTextureSubImage2D(tex_id, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateTextureMipmap(tex_id);

        glTextureParameteri(tex_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTextureParameteri(tex_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(tex_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(tex_id, GL_TEXTURE_WRAP_T, GL_REPEAT);

        GLuint64 handle = glGetTextureHandleARB(tex_id);
        glMakeTextureHandleResidentARB(handle);

        stbi_image_free(pixels);
        gpu_textures.push_back({tex_id, handle});
    }

    // populate global buffers
    SetupBatchRendering(loader, gpu_textures, default_texture);

    // create UBO
    GLuint ubo;
    glCreateBuffers(1, &ubo);
    glNamedBufferStorage(ubo, sizeof(PerFrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

    // create shader program
    const std::string vs = FileManager::ReadTextFile("../../../resources/shaders/scene/vertex.glsl");
    const std::string fs = FileManager::ReadTextFile("../../../resources/shaders/scene/fragment.glsl");
    GLuint shader_prog = ShaderManager::CreateShaderProgram(vs, fs);
    if (!shader_prog)
    {
        return EXIT_FAILURE;
    }

    while (!window.ShouldClose())
    {
        if (!window.ProcessEvents())
        {
            continue;
        }

        float time = static_cast<float>(glfwGetTime());

        const auto windowSize = window.GetSize();
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.0f, 0.0f));

        model = glm::rotate(model, time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), static_cast<float>(windowSize.width) / static_cast<float>(windowSize.height), 0.1f, 100.0f);
        glm::mat4 mvp = proj * view * model;

        glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader_prog);

        // upload transform data
        PerFrameData frame_data{mvp}; // when dealing with multiple objects best remove model from per frame matrix
        glNamedBufferSubData(ubo, 0, sizeof(PerFrameData), &frame_data);

        // bind ssbo
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, material_ssbo);
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
        glBindVertexArray(vao);

        // draw everything
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(indirect_commands.size()), 0);

        window.SwapBuffers();
    }

    window.Destroy();
    return EXIT_SUCCESS;
}