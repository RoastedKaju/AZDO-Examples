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

    Okara::GlTF::Loader loader{};
    loader.load("../../../resources/models/skull.glb");

    vertices.reserve(loader.primitive_.vertices.size());
    for (size_t i = 0; i < loader.primitive_.vertices.size(); ++i)
    {
        const auto vertex = loader.primitive_.vertices[i];
        vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z}});
    }

    indices.reserve(loader.primitive_.indices.size());
    for (size_t i = 0; i < loader.primitive_.indices.size(); ++i)
    {
        const auto index = loader.primitive_.indices[i];
        indices.push_back(index);
    }

    GLuint vao, vbo, ebo, ubo;

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

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    glVertexArrayElementBuffer(vao, ebo);

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
        glNamedBufferSubData(ubo, 0, sizeof(PerFrameData), &frame_data);

        glUseProgram(shader_prog);
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, nullptr);

        window.SwapBuffers();
    }

    window.Destroy();

    return EXIT_SUCCESS;
}