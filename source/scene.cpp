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
#include <scene_graph.hpp>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>

#include "okara_gltf_importer.h"

struct Vertex
{
    glm::vec3 position;
    glm::vec2 tex_coord;
    glm::vec4 weights;
    glm::uvec4 joints;
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

struct PerFrameData
{
    glm::mat4 view_proj;
};

struct alignas(16) PerInstanceData
{
    glm::mat4 world_matrix;
    int32_t material_index;
    int32_t padding[3] = {0, 0, 0};
};

struct PerMaterialData
{
    GLuint64 base_col_handle;
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
GLuint instance_ssbo;
GLuint joint_ssbo;

std::vector<DrawElementsIndirectCommand> indirect_commands;
std::vector<PerInstanceData> instances_gpu;
std::vector<PerMaterialData> materials_gpu;

void SetupBatchRendering(const okara::gltf::Loader &loader, const std::vector<LoadedTexture> &gpu_textures, const LoadedTexture &default_tex)
{
    std::vector<Vertex> global_vertices;
    std::vector<uint32_t> global_indices;

    // track offsets per primitive
    struct PrimitiveOffsets
    {
        uint32_t count;
        uint32_t first_index;
        int32_t base_vertex;
        uint32_t material_idx;
    };

    std::vector<std::vector<PrimitiveOffsets>> mesh_primitive_offsets(loader.meshes_.size());

    uint32_t current_first_index = 0;
    int32_t current_base_vertex = 0;

    for (size_t m_idx = 0; m_idx < loader.meshes_.size(); ++m_idx)
    {
        const auto &mesh = loader.meshes_[m_idx];
        mesh_primitive_offsets[m_idx].reserve(mesh.primitives.size());

        for (const auto &p : mesh.primitives)
        {
            // pack vertices
            for (const auto &vert : p.vertices)
            {
                global_vertices.push_back({{vert.position[0], vert.position[1], vert.position[2]},
                                           {vert.uv0[0], vert.uv0[1]},
                                           {vert.weights0[0], vert.weights0[1], vert.weights0[2], vert.weights0[3]},
                                           {vert.joints0[0], vert.joints0[1], vert.joints0[2], vert.joints0[3]}});
            }
            // pack indices
            global_indices.insert(global_indices.end(), p.indices.begin(), p.indices.end());

            // save layout offsets
            uint32_t index_count = static_cast<uint32_t>(p.indices.size());
            uint32_t vertex_count = static_cast<uint32_t>(p.vertices.size());
            uint32_t prim_mat = static_cast<uint32_t>(p.material);

            mesh_primitive_offsets[m_idx].push_back({index_count, current_first_index, current_base_vertex, prim_mat});

            // move the pointer forward
            current_first_index += index_count;
            current_base_vertex += static_cast<int32_t>(vertex_count);
        }
    }

    // build material gpu data
    for (size_t i = 0; i < loader.materials_.size(); ++i)
    {
        GLuint64 tex_handle = default_tex.handle;
        int tex_idx = loader.materials_[i].base_color_texture;
        if (tex_idx >= 0 && tex_idx < static_cast<int>(gpu_textures.size()))
        {
            tex_handle = gpu_textures[tex_idx].handle;
        }
        materials_gpu.push_back({tex_handle});
    }
    // fall back material
    if (materials_gpu.empty())
    {
        materials_gpu.push_back({default_tex.handle});
    }

    // build mesh instance ssbo and indirect draw commands
    for (const auto &instance : loader.mesh_instances_)
    {
        if (instance.mesh < 0 || instance.mesh >= static_cast<int32_t>(loader.meshes_.size()))
        {
            continue;
        }

        // convert gltf matrix to column-major layout
        glm::mat4 raw_world_matrix = glm::mat4(
            instance.world_transform[0], instance.world_transform[1], instance.world_transform[2], instance.world_transform[3],
            instance.world_transform[4], instance.world_transform[5], instance.world_transform[6], instance.world_transform[7],
            instance.world_transform[8], instance.world_transform[9], instance.world_transform[10], instance.world_transform[11],
            instance.world_transform[12], instance.world_transform[13], instance.world_transform[14], instance.world_transform[15]);

        // apply transform only once before entering the primitive loop
        glm::mat4 instance_world_matrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(10.0f)) * raw_world_matrix;

        const auto &prim_offsets = mesh_primitive_offsets[instance.mesh];

        for (const auto &offsets : prim_offsets)
        {
            uint32_t instance_id = static_cast<uint32_t>(instances_gpu.size());

            // determine valid material index
            int32_t mat_id = (offsets.material_idx < materials_gpu.size()) ? static_cast<int32_t>(offsets.material_idx) : 0;

            // push instance data to SSBO vector
            instances_gpu.push_back({instance_world_matrix, mat_id});

            // build MDI command
            DrawElementsIndirectCommand cmd{};
            cmd.count = offsets.count;
            cmd.instanceCount = 1;
            cmd.firstIndex = offsets.first_index;
            cmd.baseVertex = offsets.base_vertex;
            cmd.baseInstance = instance_id; // maps to gl_BaseInstance in GLSL

            indirect_commands.push_back(cmd);
        }
    }

    // create buffers
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

    glEnableVertexArrayAttrib(vao, 2);
    glVertexArrayAttribFormat(vao, 2, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, weights));
    glVertexArrayAttribBinding(vao, 2, 0);

    glEnableVertexArrayAttrib(vao, 3);
    glVertexArrayAttribIFormat(vao, 3, 4, GL_UNSIGNED_SHORT, offsetof(Vertex, joints));
    glVertexArrayAttribBinding(vao, 3, 0);

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao, ebo);

    // create indirect command buffer
    glCreateBuffers(1, &indirect_buffer);
    glNamedBufferStorage(indirect_buffer, indirect_commands.size() * sizeof(DrawElementsIndirectCommand), indirect_commands.data(), 0);

    // create material ssbo (binding = 1)
    glCreateBuffers(1, &material_ssbo);
    glNamedBufferStorage(material_ssbo, materials_gpu.size() * sizeof(PerMaterialData), materials_gpu.data(), 0);

    // create instance ssbo (binding = 2)
    glCreateBuffers(1, &instance_ssbo);
    glNamedBufferStorage(instance_ssbo, instances_gpu.size() * sizeof(PerInstanceData), instances_gpu.data(), GL_DYNAMIC_STORAGE_BIT);

    // create joint matrices ssbo (binding = 3)
    std::vector<glm::mat4> identity(128, glm::mat4(1.0f));
    glCreateBuffers(1, &joint_ssbo);
    glNamedBufferStorage(joint_ssbo, identity.size() * sizeof(glm::mat4), identity.data(), GL_DYNAMIC_STORAGE_BIT);
}

void UpdateAnimation(okara::gltf::Loader &loader, float animation_time)
{
    if (loader.animations_.empty())
    {
        return;
    }

    const auto &anim = loader.animations_[0];

    for (const auto &channel : anim.channels)
    {
        if (channel.target_node < 0 || channel.target_node >= loader.nodes_.size())
        {
            continue;
        }

        if (channel.sampler < 0 || channel.sampler >= anim.samplers.size())
        {
            continue;
        }

        const auto &sampler = anim.samplers[channel.sampler];
        if (sampler.timestamps.empty())
        {
            continue;
        }

        // wrap of clamp animation time
        float max_time = sampler.timestamps.back();
        float t = std::fmod(animation_time, max_time);

        // find keyframe indices
        size_t next_idx = 0;
        while (next_idx < sampler.timestamps.size() && sampler.timestamps[next_idx] < t)
        {
            next_idx++;
        }

        size_t prev_idx = (next_idx == 0) ? 0 : next_idx - 1;
        if (next_idx >= sampler.timestamps.size())
        {
            next_idx = prev_idx;
        }

        float t0 = sampler.timestamps[prev_idx];
        float t1 = sampler.timestamps[next_idx];
        float factor = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;

        auto &node = loader.nodes_[channel.target_node];
        node.has_matrix = false; // force re-composition

        if (channel.target_path == "translation" && sampler.keyframe_data.size() >= (next_idx * 3 + 3))
        {
            glm::vec3 p0(sampler.keyframe_data[prev_idx * 3 + 0], sampler.keyframe_data[prev_idx * 3 + 1], sampler.keyframe_data[prev_idx * 3 + 2]);
            glm::vec3 p1(sampler.keyframe_data[next_idx * 3 + 0], sampler.keyframe_data[next_idx * 3 + 1], sampler.keyframe_data[next_idx * 3 + 2]);
            glm::vec3 val = glm::mix(p0, p1, factor);
            node.translation = {val.x, val.y, val.z};
        }
        else if (channel.target_path == "rotation" && sampler.keyframe_data.size() >= (next_idx * 4 + 4))
        {
            glm::quat r0(sampler.keyframe_data[prev_idx * 4 + 3], sampler.keyframe_data[prev_idx * 4 + 0], sampler.keyframe_data[prev_idx * 4 + 1], sampler.keyframe_data[prev_idx * 4 + 2]); // glTF order is x,y,z,w
            glm::quat r1(sampler.keyframe_data[next_idx * 4 + 3], sampler.keyframe_data[next_idx * 4 + 0], sampler.keyframe_data[next_idx * 4 + 1], sampler.keyframe_data[next_idx * 4 + 2]);
            glm::quat val = glm::slerp(r0, r1, factor);
            node.rotation = {val.x, val.y, val.z, val.w};
        }
        else if (channel.target_path == "scale" && sampler.keyframe_data.size() >= (next_idx * 3 + 3))
        {
            glm::vec3 s0(sampler.keyframe_data[prev_idx * 3 + 0], sampler.keyframe_data[prev_idx * 3 + 1], sampler.keyframe_data[prev_idx * 3 + 2]);
            glm::vec3 s1(sampler.keyframe_data[next_idx * 3 + 0], sampler.keyframe_data[next_idx * 3 + 1], sampler.keyframe_data[next_idx * 3 + 2]);
            glm::vec3 val = glm::mix(s0, s1, factor);
            node.scale = {val.x, val.y, val.z};
        }
    }
}

int main()
{
    // Exercise();

    WindowManager::Window window{};
    window.Create("Scene", 800, 600);
    window.EnableOutputDebug();

    glEnable(GL_DEPTH_TEST);

    // default fallback texture if a material lacks a texture
    LoadedTexture default_texture = CreateFallbackTexture();

    okara::gltf::Loader loader{};
    loader.LoadScene("../../../resources/models/Untitled.glb");

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

        glm::mat4 scene_rotation = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 1000.0f), glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), static_cast<float>(windowSize.width) / static_cast<float>(windowSize.height), 0.1f, 3000.0f);
        glm::mat4 view_proj = proj * view * scene_rotation;

        glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glUseProgram(shader_prog);

        // eval animation
        UpdateAnimation(loader, time);

        // compute joint matrices
        if (!loader.skins_.empty())
        {
            const auto &skin = loader.skins_[0];
            std::vector<glm::mat4> joint_matrices(skin.joints.size());

            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                auto raw_mat = loader.CalculateJointMatrix(skin, j);

                // convert matrix to GLM
                joint_matrices[j] = glm::mat4(
                    raw_mat[0], raw_mat[1], raw_mat[2], raw_mat[3],
                    raw_mat[4], raw_mat[5], raw_mat[6], raw_mat[7],
                    raw_mat[8], raw_mat[9], raw_mat[10], raw_mat[11],
                    raw_mat[12], raw_mat[13], raw_mat[14], raw_mat[15]);
            }

            // upload to ssbo
            glNamedBufferSubData(joint_ssbo, 0, joint_matrices.size() * sizeof(glm::mat4), joint_matrices.data());
        }

        // upload transform data
        PerFrameData frame_data{view_proj};
        glNamedBufferSubData(ubo, 0, sizeof(PerFrameData), &frame_data);

        // bind SSBOs
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, material_ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, instance_ssbo);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, joint_ssbo);

        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect_buffer);
        glBindVertexArray(vao);

        // draw everything
        glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(indirect_commands.size()), 0);

        window.SwapBuffers();
    }

    window.Destroy();
    return EXIT_SUCCESS;
}