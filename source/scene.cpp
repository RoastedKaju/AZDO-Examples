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
#include <stb_image.h>
#include <iostream>
#include <common.hpp>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>
#include <okaraGlTF.h>

struct Vertex
{
    glm::vec3 position;
    glm::vec2 tex_coord;
};

struct PerFrameData
{
    glm::mat4 model_view_proj;
    GLuint64 base_color_handle;
};

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

int main()
{
    WindowManager::Window window{};
    window.Create("Scene", 800, 600);
    window.EnableOutputDebug();

    glEnable(GL_DEPTH_TEST);

    // load mesh data
    Okara::GlTF::Loader loader{};
    loader.load("../../../resources/models/skull.glb");

    vertices.reserve(loader.primitive_.vertices.size());
    for (size_t i = 0; i < loader.primitive_.vertices.size(); ++i)
    {
        const auto vertex = loader.primitive_.vertices[i];
        const glm::vec3 pos = {vertex.position.x, vertex.position.y, vertex.position.z};
        const glm::vec2 uv = {vertex.uv.x, vertex.uv.y};
        vertices.push_back({pos, uv});
    }

    indices.reserve(loader.primitive_.indices.size());
    for (size_t i = 0; i < loader.primitive_.indices.size(); ++i)
    {
        const auto index = loader.primitive_.indices[i];
        indices.push_back(index);
    }

    const Okara::GlTF::Material &mat = loader.materials_[loader.primitive_.material];
    const Okara::GlTF::Texture &base_col_tex = loader.textures_[mat.base_color_texture];
    const Okara::GlTF::Image &src_img = loader.images_[base_col_tex.source];
    const Okara::GlTF::BufferView &bv = loader.buffer_views_[src_img.buffer_view];

    int w, h, c;
    unsigned char *pixels = stbi_load_from_memory((const unsigned char *)src_img.data.data(), src_img.data.size(), &w, &h, &c, 4);
    if (!pixels)
    {
        std::cout << "Failed to load image data.\n";
        return EXIT_FAILURE;
    }

    // create buffers and upload mesh data to GPU
    GLuint vao, vbo, ebo, ubo;
    GLuint base_col_tex_id;
    GLuint64 base_col_handle;

    glCreateBuffers(1, &vbo);
    glNamedBufferStorage(vbo, vertices.size() * sizeof(Vertex), vertices.data(), 0);

    glCreateBuffers(1, &ebo);
    glNamedBufferStorage(ebo, indices.size() * sizeof(uint32_t), indices.data(), 0);

    glCreateBuffers(1, &ubo);
    glNamedBufferStorage(ubo, sizeof(PerFrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

    glCreateVertexArrays(1, &vao);

    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
    glVertexArrayAttribBinding(vao, 0, 0);

    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, tex_coord));
    glVertexArrayAttribBinding(vao, 1, 0);

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao, ebo);

    GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(w, h)))) + 1;
    glCreateTextures(GL_TEXTURE_2D, 1, &base_col_tex_id);
    glTextureStorage2D(base_col_tex_id, levels, GL_RGBA8, w, h);
    glTextureSubImage2D(base_col_tex_id, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateTextureMipmap(base_col_tex_id);
    glTextureParameteri(base_col_tex_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(base_col_tex_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(base_col_tex_id, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(base_col_tex_id, GL_TEXTURE_WRAP_T, GL_REPEAT);

    base_col_handle = glGetTextureHandleARB(base_col_tex_id);
    glMakeTextureHandleResidentARB(base_col_handle);

    stbi_image_free(pixels);

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

        float time = (float)glfwGetTime();

        const auto windowSize = window.GetSize();
        glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)windowSize.width / (float)windowSize.height, 0.1f, 100.0f);
        glm::mat4 mvp = proj * view * model;

        glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        PerFrameData frame_data{};
        frame_data.model_view_proj = mvp;
        frame_data.base_color_handle = base_col_handle;
        glNamedBufferSubData(ubo, 0, sizeof(PerFrameData), &frame_data);

        glUseProgram(shader_prog);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, nullptr);

        window.SwapBuffers();
    }

    window.Destroy();

    return EXIT_SUCCESS;
}