#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <window_manager.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>
#include <iostream>
#include <vector>
#include <string>


const char* vertexShaderSource = R"(
	#version 460 core
	#extension GL_ARB_gpu_shader_int64 : require
	layout (location = 0) in vec3 aPos;
	layout (location = 1) in vec2 aTexCoord;

	layout (location = 0) out vec2 vTexCoord;

	layout (std140, binding = 0)
	uniform FrameData {
		mat4 MVP;
	};

	void main() {
		vTexCoord = aTexCoord;
		gl_Position = MVP * vec4(aPos, 1.0);
	}
)";

const char* fragmentShaderSource = R"(
	#version 460 core
	#extension GL_ARB_bindless_texture : require
	#extension GL_ARB_gpu_shader_int64 : require
	layout (location = 0) in vec2 vTexCoord;

	layout (location = 0) out vec4 FragColor;

	layout(std430, binding = 0)
	buffer ObjectData {
		uvec2 colorHandle;
	} objectData;

	void main() {
		sampler2D baseColorSampler = sampler2D(objectData.colorHandle);

		FragColor = texture(baseColorSampler, vTexCoord);
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
		exit(1);
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
		exit(1);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	return program;
}

struct Vertex
{
	glm::vec3 position;
	glm::vec2 texCoord;
};
// Quad geometry
Vertex vertices[] = {
	{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
	{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
	{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } },
	{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } }
};

GLuint indices[] = {
	0, 1, 2,
	2, 3, 0
};

int main()
{
	WindowManager::Window window{};
	window.CreateWindow("Texture", 800, 600);
	window.EnableOutputDebug();

	// Depth Testing
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	// Culling
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	// Buffers
	GLuint VBO, EBO, UBO, SSBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(indices), indices, GL_DYNAMIC_STORAGE_BIT);

	// For per frame data
	glCreateBuffers(1, &UBO);
	glNamedBufferStorage(UBO, sizeof(glm::mat4), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, UBO);
	
	// For per object data
	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(GLuint64), nullptr, GL_DYNAMIC_STORAGE_BIT);
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

	// Load image from file
	ImageData image{};
	if (!FileManager::ReadImageFile("../../../resources/textures/uv_tiles.png", image, false))
	{
		return EXIT_FAILURE;
	}

	// Create texture
	GLuint debugTexture;
	// Calculate required mipmap levels dynamically
	GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(image.width, image.height)))) + 1;
	glCreateTextures(GL_TEXTURE_2D, 1, &debugTexture);
	glTextureStorage2D(debugTexture, levels, GL_RGBA8, image.width, image.height);
	glTextureSubImage2D(debugTexture, 0, 0, 0, image.width, image.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
	// Enable mipmapping and filtering
	glGenerateTextureMipmap(debugTexture);
	glTextureParameteri(debugTexture, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTextureParameteri(debugTexture, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTextureParameteri(debugTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(debugTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// Bindless texture
	GLuint64 textureHandle = glGetTextureHandleARB(debugTexture);
	glMakeTextureHandleResidentARB(textureHandle);

	// Upload texture handle to SSBO once
	glNamedBufferSubData(SSBO, 0, sizeof(GLuint64), &textureHandle);

	while (!window.ShouldClose())
	{
		if (!window.ProcessEvents())
		{
			continue;
		}

		float time = (float)glfwGetTime();


		float cameraZ = std::sin(time) + 2.5f;

		const auto windowSize = window.GetSize();

		glm::mat4 model = glm::mat4(1.0f);
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, cameraZ), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)windowSize.width / (float)windowSize.height, 0.1f, 100.0f);
		glm::mat4 mvp = proj * view * model;

		// Upload MVP per frame to UBO
		glNamedBufferSubData(UBO, 0, sizeof(glm::mat4), &mvp);

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

		window.SwapBuffers();
	}

	glMakeTextureHandleNonResidentARB(textureHandle);
	glDeleteProgram(shaderProgram);
	glDeleteTextures(1, &debugTexture);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &UBO);
	glDeleteBuffers(1, &SSBO);

	window.DestroyWindow();

	return EXIT_SUCCESS;
}