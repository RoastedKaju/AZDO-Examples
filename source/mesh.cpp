#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
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

const char* vertexShaderSource = R"(
	#version 460 core
	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec3 aColor;

	layout (location = 0) out vec3 vColor;

	struct DrawParams {
		mat4 mvp;
		vec4 wireframeColor;
	};

	layout (std430, binding = 0)
	buffer SceneData { DrawParams data; };

	void main() {
		vColor = aColor;
		gl_Position = data.mvp * vec4(aPos, 1.0);
	}
)";

const char* fragmentShaderSource = R"(
	#version 460 core
	layout (location = 0) in vec3 vColor;
	layout (location = 0) out vec4 FragColor;

	struct DrawParams {
		mat4 mvp;
		vec4 wireframeColor;
	};

	layout (std430, binding = 0)
	buffer SceneData { DrawParams data; };

	void main() {
		if(data.wireframeColor.a > 0.0) {
			FragColor = data.wireframeColor;
		} else {
		FragColor = vec4(vColor, 1.0);
		}
	}
)";

static GLuint CreateShaderProgram()
{
	GLuint vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, &vertexShaderSource, nullptr);
	glCompileShader(vs);

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, &fragmentShaderSource, nullptr);
	glCompileShader(fs);

	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);
	return program;
}

int main()
{
	if (!glfwInit())
	{
		return EXIT_FAILURE;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Mesh", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return EXIT_FAILURE;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	glfwSetFramebufferSizeCallback(window, &FrameBufferSizeCallback);

	int version = gladLoadGL(glfwGetProcAddress);
	if (!version) return EXIT_FAILURE;

	glViewport(0, 0, sWidth, sHeight);
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(glDebugOutput, nullptr);

	// enable depth testing
	glEnable(GL_DEPTH_TEST);

	// Buffer struct, must match the types in shader
	struct DrawParams
	{
		glm::mat4 mvp;
		glm::vec4 wireframeColor;
	};

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

	// DSA buffers
	GLuint VBO, EBO, SSBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(indices), indices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(DrawParams), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SSBO);

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

	GLuint shaderProgram = CreateShaderProgram();

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float time = (float)glfwGetTime();

		glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.5f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);

		glm::mat4 mvp = proj * view * model;

		// AZDO stream: update SSBO directly via DSA
		DrawParams params{};
		params.mvp = mvp;
		glNamedBufferSubData(SSBO, 0, sizeof(DrawParams), &params);

		// Clear
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);

		// Render wireframe
		params.wireframeColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		glNamedBufferSubData(SSBO, 0, sizeof(DrawParams), &params);
		glEnable(GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1.0f, -1.0f);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glLineWidth(2.0f);
		glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
		// Restore default state
		glDisable(GL_POLYGON_OFFSET_LINE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glfwSwapBuffers(window);
	}

	glDeleteProgram(shaderProgram);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &SSBO);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}