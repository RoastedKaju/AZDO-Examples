#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>
#include <iostream>

// Vertex
struct Vertex
{
	glm::vec3 position;
	glm::vec3 color;
};

Vertex vertices[] = {
	{ { -0.5f, -0.5f,  0.5f }, { 1.0f, 0.0f, 0.0f } }, // 0: Bottom-Left-Front
	{ {  0.5f, -0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } }, // 1: Bottom-Right-Front
	{ {  0.5f,  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f } }, // 2: Top-Right-Front
	{ { -0.5f,  0.5f,  0.5f }, { 1.0f, 1.0f, 0.0f } }, // 3: Top-Left-Front
	{ { -0.5f, -0.5f, -0.5f }, { 1.0f, 0.0f, 1.0f } }, // 4: Bottom-Left-Back
	{ {  0.5f, -0.5f, -0.5f }, { 0.0f, 1.0f, 1.0f } }, // 5: Bottom-Right-Back
	{ {  0.5f,  0.5f, -0.5f }, { 1.0f, 1.0f, 1.0f } }, // 6: Top-Right-Back
	{ { -0.5f,  0.5f, -0.5f }, { 0.2f, 0.2f, 0.2f } }  // 7: Top-Left-Back
};

GLuint indices[] = {
	// Front
	0, 1, 2,  2, 3, 0,
	// Right
	1, 5, 6,  6, 2, 1,
	// Back
	5, 4, 7,  7, 6, 5,
	// Left
	4, 0, 3,  3, 7, 4,
	// Top
	3, 2, 6,  6, 7, 3,
	// Bottom
	4, 5, 1,  1, 0, 4
};

// Buffer struct, must match the types in shader
struct FrameData
{
	glm::mat4 mvp;
	glm::vec4 wireframeColor;
};

int main()
{
	WindowManager::Window window{};
	window.CreateWindow("Mesh", 800, 600);
	window.EnableOutputDebug();

	glEnable(GL_DEPTH_TEST);

	// buffers
	GLuint VBO, EBO, UBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(indices), indices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &UBO);
	glNamedBufferStorage(UBO, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, UBO);

	// VAO
	GLuint VAO;
	glCreateVertexArrays(1, &VAO);

	glEnableVertexArrayAttrib(VAO, 0);
	glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribBinding(VAO, 0, 0);

	glEnableVertexArrayAttrib(VAO, 1);
	glVertexArrayAttribFormat(VAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
	glVertexArrayAttribBinding(VAO, 1, 0);

	glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(Vertex)); // attach VBO to VAO
	glVertexArrayElementBuffer(VAO, EBO); // attach EBO to VAO

	// Load and create shader program
	const std::string vs = FileManager::ReadTextFile("../../../resources/shaders/mesh/vertex.glsl");
	const std::string fs = FileManager::ReadTextFile("../../../resources/shaders/mesh/fragment.glsl");
	GLuint shaderProgram = ShaderManager::CreateShaderProgram(vs, fs);
	if (!shaderProgram)
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
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)windowSize.width / (float)windowSize.height, 0.1f, 100.0f);

		glm::mat4 mvp = proj * view * model;

		FrameData params{};
		params.mvp = mvp;
		glNamedBufferSubData(UBO, 0, sizeof(FrameData), &params);

		// Clear
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

		// Render wireframe
		params.wireframeColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glNamedBufferSubData(UBO, 0, sizeof(FrameData), &params);
		glEnable(GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1.0f, -1.0f);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(2.0f);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
		// Restore default state
		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		window.SwapBuffers();
	}

	glDeleteProgram(shaderProgram);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &UBO);

	window.Destroy();

	return EXIT_SUCCESS;
}