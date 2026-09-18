#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb.hpp>
#include <iostream>
#include <vector>
#include <string>

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
	#extension GL_ARB_gpu_shader_int64 : require
	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec2 aTexCoord;

	layout (location = 0) out vec2 vTexCoord;

	struct FrameData {
		mat4 MVP;
		uvec2 textureHandle;
	};

	layout (std430, binding = 0)
	buffer SceneData {
		FrameData frameData;
	};

	void main() {
		vTexCoord = aTexCoord;
		gl_Position = frameData.MVP * vec4(aPos, 1.0);
	}
)";

const char* fragmentShaderSource = R"(
	#version 460 core
	#extension GL_ARB_bindless_texture : require
	#extension GL_ARB_gpu_shader_int64 : require
	layout (location = 0) in vec2 vTexCoord;

	layout (location = 0) out vec4 FragColor;

	struct FrameData {
		mat4 MVP;
		uvec2 textureHandle; // Keep this GLuint64 on CPU side (bindless handle)
	};

	layout (std430, binding = 0)
	buffer SceneData {
		FrameData frameData;
	};

	void main() {
		sampler2D bindlessTex = sampler2D(frameData.textureHandle);

		FragColor = texture(bindlessTex, vTexCoord);
	}
)";

static GLuint CompileShader(GLenum type, const char* source)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint success = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLint logLen = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
		std::string log(logLen, '\0');
		glGetShaderInfoLog(shader, logLen, nullptr, log.data());
		std::cerr << "Shader compile error (" << (type == GL_VERTEX_SHADER ? "vertex" : "fragment") << "):\n" << log << std::endl;
	}
	return shader;
}

static GLuint CreateShaderProgram()
{
	GLuint vs = CompileShader(GL_VERTEX_SHADER, vertexShaderSource);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);

	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked)
	{
		GLint logLen = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
		std::string log(logLen, '\0');
		glGetProgramInfoLog(program, logLen, nullptr, log.data());
		std::cerr << "Program link error:\n" << log << std::endl;
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	return program;
}

static bool LoadImage(const char* path, std::vector<char>& outPixels, int& outWidth, int& outHeight)
{
	int width = 0, height = 0, sourceChannels = 0;
	stbi_set_flip_vertically_on_load(true);
	unsigned char* data = stbi_load(path, &width, &height, &sourceChannels, 4);
	if (!data)
	{
		std::cerr << "Failed to load image: " << path << " (" << stbi_failure_reason() << ")" << std::endl;
		return false;
	}

	outWidth = width;
	outHeight = height;
	outPixels.assign(data, data + (static_cast<size_t>(width) * height * 4));

	stbi_image_free(data);
	return true;
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

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Textured Quad", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return EXIT_FAILURE;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, &FrameBufferSizeCallback);

	int version = gladLoadGL(glfwGetProcAddress);
	if (!version) return EXIT_FAILURE;

	// Verify GPU driver supports bindless textures
	if (!GLAD_GL_ARB_bindless_texture)
	{
		std::cerr << "GL_ARB_bindless_texture is NOT supported on this GPU!" << std::endl;
		return EXIT_FAILURE;
	}
	if (!GLAD_GL_ARB_gpu_shader_int64)
	{
		std::cerr << "GL_ARB_gpu_shader_int64 is NOT supported!\n";
		return EXIT_FAILURE;
	}

	glViewport(0, 0, sWidth, sHeight);
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(glDebugOutput, nullptr);

	glEnable(GL_DEPTH_TEST);

	// C++ Mirror Struct matching SSBO layout
	struct FrameData
	{
		glm::mat4 mvp;
		GLuint64 textureHandle; // 64-bit GPU address
	};

	struct Vertex
	{
		glm::vec3 position;
		glm::vec2 texCoord;
	};
	// Quad geometry
	Vertex vertices[] = {
		{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
		{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
		{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 1.0f } },
		{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f } }
	};

	GLuint indices[] = {
		0, 1, 2,
		2, 3, 0
	};

	GLuint VBO, EBO, SSBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(indices), indices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SSBO);

	// VAO
	GLuint VAO;
	glCreateVertexArrays(1, &VAO);

	glEnableVertexArrayAttrib(VAO, 0);
	glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribBinding(VAO, 0, 0);

	glEnableVertexArrayAttrib(VAO, 1);
	glVertexArrayAttribFormat(VAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
	glVertexArrayAttribBinding(VAO, 1, 0);

	glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(Vertex)); // attach VBO to VAO
	glVertexArrayElementBuffer(VAO, EBO); // attach EBO to VAO

	GLuint shaderProgram = CreateShaderProgram();

	std::vector<char> pixels;
	int width;
	int height;
	if (!LoadImage("../../../resources/textures/uv_tiles.png", pixels, width, height))
	{
		glfwDestroyWindow(window);
		glfwTerminate();
		return EXIT_FAILURE;
	}
	// Create texture
	GLuint debugTexture;
	// Calculate required mipmap levels dynamically
	GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(width, height)))) + 1;
	glCreateTextures(GL_TEXTURE_2D, 1, &debugTexture);
	glTextureStorage2D(debugTexture, levels, GL_RGBA8, width, height);
	glTextureSubImage2D(debugTexture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	// Enable mipmapping and filtering
	glGenerateTextureMipmap(debugTexture);
	glTextureParameteri(debugTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTextureParameteri(debugTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTextureParameteri(debugTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(debugTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// Bindless texture
	GLuint64 textureHandle = glGetTextureHandleARB(debugTexture);
	glMakeTextureHandleResidentARB(textureHandle);

	FrameData frameData{};
	frameData.textureHandle = textureHandle;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float time = (float)glfwGetTime();

		float cameraZ = std::sin(time) + 2.5f;

		glm::mat4 model = glm::mat4(1.0f);
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);

		glm::mat4 mvp = proj * view * model;

		// Direct stream MVP to SSBO
		frameData.mvp = mvp;
		glNamedBufferSubData(SSBO, 0, sizeof(FrameData), &frameData);

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

		glfwSwapBuffers(window);
	}

	glMakeTextureHandleNonResidentARB(textureHandle);
	glDeleteProgram(shaderProgram);
	glDeleteTextures(1, &debugTexture);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &SSBO);
	glfwDestroyWindow(window);
	glfwTerminate();
}