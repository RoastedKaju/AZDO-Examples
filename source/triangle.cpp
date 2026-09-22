#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>
#include <iostream>

static int sWidth = 800;
static int sHeight = 600;

void GLAPIENTRY glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	// Ignore non-significant error codes
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;
	std::cerr << "GL Debug [" << id << "] " << message << '\n';
}

static void FrameBufferSizeCallback(GLFWwindow* window, int width, int height)
{
	sWidth = width;
	sHeight = height;
	glViewport(0, 0, width, height);
}

int main()
{
	if (!glfwInit())
	{
		std::cerr << "Failed to Initialize GLFW.\n";
		return EXIT_FAILURE;
	}

	// Request OpenGL 4.6 Core profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Triangle", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return EXIT_FAILURE;
	}
	glfwMakeContextCurrent(window);

	glfwSetFramebufferSizeCallback(window, &FrameBufferSizeCallback);

	// Initialize GLAD
	int version = gladLoadGL(glfwGetProcAddress);
	if (!version)
	{
		std::cerr << "Failed to initialize GLAD.\n";
		return EXIT_FAILURE;
	}
	// Check extensions
	if (!GLAD_GL_ARB_bindless_texture)
	{
		std::cerr << "GL_ARB_bindless_texture not supported.\n";
		return EXIT_FAILURE;
	}
	if (!GLAD_GL_ARB_gpu_shader_int64)
	{
		std::cerr << "GL_ARB_gpu_shader_int64 not supported.\n";
		return EXIT_FAILURE;
	}

	glfwGetFramebufferSize(window, &sWidth, &sHeight);
	glViewport(0, 0, sWidth, sHeight);

	// Enable OpenGL debug output
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(glDebugOutput, nullptr);

	// Define vertex data
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

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (sWidth == 0 || sHeight == 0)
		{
			glfwWaitEvents();
			continue;
		}

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		// Render
		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		glfwSwapBuffers(window);
	}

	glDeleteProgram(shaderProgram);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glfwDestroyWindow(window);
	glfwTerminate();

	return EXIT_SUCCESS;
}