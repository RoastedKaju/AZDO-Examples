#include <glad/gl.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>
#include <window_manager.hpp>

// Triangle vertex structure and data
struct Vertex
{
	glm::vec3 position;
	glm::vec3 color;
};

Vertex vertices[] = {
	{ { -0.5f, -0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f } }, // Bottom Left (Red)
	{ {  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f } }, // Bottom Right (Green)
	{ {  0.0f,  0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f } }  // Top Center (Blue)
};

int main()
{
	// Create window
	WindowManager::Window window{};
	window.CreateWindow("Triangle", 800, 600);
	window.EnableOutputDebug();

	FileManager::LoadGlTF("../../../resources/models/ibex.glb");

	// Create VBO with DSA
	GLuint VBO;
	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, 0);

	// Create VAO
	GLuint VAO;
	glCreateVertexArrays(1, &VAO);

	// Attributes
	glEnableVertexArrayAttrib(VAO, 0);
	glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribBinding(VAO, 0, 0); // bind attribute 0 to binding point 0

	glEnableVertexArrayAttrib(VAO, 1);
	glVertexArrayAttribFormat(VAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
	glVertexArrayAttribBinding(VAO, 1, 0);

	// Connect VBO to binding point 0 on VAO
	glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(Vertex));

	// Read shader files and create shader program
	std::string vs = FileManager::ReadTextFile("../../../resources/shaders/triangle/vertex.glsl");
	std::string fs = FileManager::ReadTextFile("../../../resources/shaders/triangle/fragment.glsl");
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

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Render
		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		window.SwapBuffers();
	}

	glDeleteProgram(shaderProgram);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);

	window.DestroyWindow();

	return EXIT_SUCCESS;
}