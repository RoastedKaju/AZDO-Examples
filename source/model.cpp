#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb.hpp>
#include <tiny_gltf_v3.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

void GLAPIENTRY glDebugOutput(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
	// Ignore non-significant error codes
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;
	std::cerr << "GL Debug [" << id << "] " << message << '\n';
}

static void FrameBufferSizeCallback(GLFWwindow* window, int width, int height)
{
	glViewport(0, 0, width, height);
}

struct Vertex
{
	glm::vec3 position;
	glm::vec3 texCoord;
};

struct FrameData
{
	glm::mat4 MVP;
	GLuint64 diffuseHandle;
};

struct Mesh
{
	GLuint VAO = 0, VBO = 0, EBO = 0;
	GLsizei indexCount = 0;
	GLuint textureId = 0;
	GLuint64 textureHandle = 0;
};

struct Model
{
	std::vector<Mesh> meshes;
};

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

layout (std430, binding = 0) buffer SceneData {
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
	uvec2 textureHandle;
};

layout (std430, binding = 0) buffer SceneData {
	FrameData frameData;
};

void main() {
	uvec2 handleVec = unpackUint2x32(frameData.textureHandle);
	sampler2D bindlessTex = sampler2D(handleVec);
	FragColor = texture(bindlessTex, vTexCoord);
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

static bool LoadImageFromMemory(const unsigned char* data, size_t size, GLuint& outTexId, GLuint64& outHandle)
{
	int width, height, channels;
	stbi_set_flip_vertically_on_load(true);
	unsigned char* pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
	if (!pixels)
	{
		std::cerr << "STBI load from memory failed: " << stbi_failure_reason() << std::endl;
		return false;
	}

	GLuint texture;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);
	glTextureStorage2D(texture, 1, GL_RGBA8, width, height);
	glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_REPEAT);

	stbi_image_free(pixels);

	GLuint64 handle = glGetTextureHandleARB(texture);
	if (handle == 0)
	{
		std::cerr << "Get Texture Handle failed" << std::endl;
		glDeleteTextures(1, &texture);
		return false;
	}
	glMakeTextureHandleResidentARB(handle);

	outTexId = texture;
	outHandle = handle;
	return true;
}

static void LoadModel(const std::string& filePath)
{
	if (!std::filesystem::exists(filePath))
	{
		std::cerr << "File does not exist: " << filePath << std::endl;
		return;
	}

	std::cout << "Loading GLTF: " << filePath << std::endl;
	tg3_model model;
	tg3_parse_options opts;
	tg3_error_stack errors;

	tg3_parse_options_init(&opts);
	tg3_error_stack_init(&errors);
	tg3_error_code parseResult = tg3_parse_file(&model, &errors, filePath.c_str(), (uint32_t)filePath.size(), &opts);
	if (parseResult != TG3_OK)
	{
		std::cout << "Error parsing GLTF file: \n";
		for (uint32_t i = 0; i < errors.count; ++i)
		{
			std::cout << errors.entries[i].message << std::endl;
		}
		tg3_error_stack_free(&errors);
		return;
	}
	tg3_error_stack_free(&errors);
}

int main()
{
	if (!glfwInit()) return EXIT_FAILURE;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(800, 600, "Model", nullptr, nullptr);
	if (!window)
	{
		glfwTerminate();
		return EXIT_FAILURE;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, &FrameBufferSizeCallback);

	int version = gladLoadGL(glfwGetProcAddress);
	if (!version || !GLAD_GL_ARB_bindless_texture)
	{
		std::cerr << "Required OpenGL 4.6 or bindless texture unsupported!" << std::endl;
		return EXIT_FAILURE;
	}

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(glDebugOutput, nullptr);

	// enable depth testing
	glEnable(GL_DEPTH_TEST);

	GLuint shaderProgram = CreateShaderProgram();

	// SSBO holding per-frame data
	GLuint SSBO;
	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SSBO);

	// Load Model

	FrameData frameData{};

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float time = (float)glfwGetTime();

		// MVP
		glm::mat4 model = glm::mat4(1.0f);
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 projection = glm::perspective(glm::radians(60.0f), 800.0f / 600.0f, 0.1f, 100.0f);
		glm::mat4 MVP = projection * view * model;

		// Direct stream MVP to SSBO
		frameData.MVP = MVP;
		glNamedBufferSubData(SSBO, 0, sizeof(FrameData), &frameData);

		// Clear color
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glfwSwapBuffers(window);
	}


	glDeleteBuffers(1, &SSBO);
	glDeleteProgram(shaderProgram);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}