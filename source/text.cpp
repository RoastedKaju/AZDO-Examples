#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_truetype.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <format>

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
		exit(EXIT_FAILURE);
	}
	return shader;
}

static GLuint CreateShaderProgram(const char* vertexShaderSource, const char* fragmentShaderSource)
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
		exit(EXIT_FAILURE);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	return program;
}

const char* unlitVertex = R"(
#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec3 vColor;

layout (std430, binding = 0) readonly buffer PerDrawData{
	mat4 uMVP;
};

void main() {
	vColor = aColor;
	gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

const char* unlitFrag = R"(
#version 460 core

layout (location = 0) in vec3 vColor;
layout (location = 0) out vec4 FragColor;

void main() {
	FragColor = vec4(vColor, 1.0);
}
)";

const char* textVertexShader = R"(
#version 460 core
#extension GL_ARB_bindless_texture : require

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in vec4 aColor;

layout (location = 0) out vec2 vUV;
layout (location = 1) out vec4 vColor;

layout (std430, binding = 1) readonly buffer PerDrawData {
	mat4 uProj;
	uvec2 uTexHandle;
};

void main() {
	vUV = aUV;
	vColor = aColor;
	gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)";

const char* textFragShader = R"(
#version 460 core
#extension GL_ARB_bindless_texture : require

layout (location = 0) in vec2 vUV;
layout (location = 1) in vec4 vColor;
layout (location = 0) out vec4 FragColor;

layout (std430, binding = 1) readonly buffer PerDrawData {
	mat4 uProj;
	uvec2 uTexHandle;
};

void main() {
	sampler2D baseTex = sampler2D(uTexHandle);
	float alpha = texture(baseTex, vUV).r;
	FragColor = vec4(vColor.rgb, alpha * vColor.a);
}
)";

// Text rendering types
struct TextVertex
{
	glm::vec2 position;
	glm::vec2 uv;
	glm::vec4 color;
};

struct PerDrawTextData
{
	glm::mat4 proj;
	GLuint64 textureHandle;
};

struct TextRenderer
{
	GLuint texture = 0;
	GLuint64 textureHandle = 0;
	GLuint program = 0;
	GLuint VAO = 0, VBO = 0, SSBO = 0;
	stbtt_packedchar charData[96]{}; // ASCII 32..127
	int atlasWidth = 512;
	int atlasHeight = 512;
	float fontSize = 32.0f;
	size_t maxQuads = 4069; // capacity of dynamic VBO, in quads (6 verts each)

	// Per-frame accumulator, cleared not reallocated in begin frame function
	std::vector<TextVertex> frameVerts;
	glm::mat4 frameProj{};

	bool Init(const char* fontPath)
	{
		std::ifstream file(fontPath, std::ios::binary | std::ios::ate);
		if (!file)
		{
			std::cerr << "Failed to open font file: " << fontPath << std::endl;
			return false;
		}

		std::streamsize size = file.tellg();
		file.seekg(0, std::ios::beg);
		std::vector<unsigned char> ttfBuffer((size_t)size);
		if (!file.read(reinterpret_cast<char*>(ttfBuffer.data()), size))
		{
			std::cerr << "Failed to read font: " << fontPath << std::endl;
			return false;
		}

		// bake atlas
		std::vector<unsigned char> bitmap((size_t)atlasWidth * atlasHeight);
		stbtt_pack_context packCtx;
		if (!stbtt_PackBegin(&packCtx, bitmap.data(), atlasWidth, atlasHeight, 0, 1, nullptr))
		{
			std::cerr << "STB TTF pack begin failed.\n";
			return false;
		}
		stbtt_PackSetOversampling(&packCtx, 2, 2);
		stbtt_PackFontRange(&packCtx, ttfBuffer.data(), 0, fontSize, 32, 96, charData);
		stbtt_PackEnd(&packCtx);

		// Upload atlas as single channel texture
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);
		glTextureStorage2D(texture, 1, GL_R8, atlasWidth, atlasHeight);
		glTextureSubImage2D(texture, 0, 0, 0, atlasWidth, atlasHeight, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		textureHandle = glGetTextureHandleARB(texture);
		glMakeTextureHandleResidentARB(textureHandle);

		// Shader
		program = CreateShaderProgram(textVertexShader, textFragShader);

		glCreateBuffers(1, &VBO);
		glNamedBufferStorage(VBO, maxQuads * 6 * sizeof(TextVertex), nullptr, GL_DYNAMIC_STORAGE_BIT);

		glCreateBuffers(1, &SSBO);
		glNamedBufferStorage(SSBO, sizeof(PerDrawTextData), nullptr, GL_DYNAMIC_STORAGE_BIT);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, SSBO);

		glCreateVertexArrays(1, &VAO);
		glEnableVertexArrayAttrib(VAO, 0);
		glVertexArrayAttribFormat(VAO, 0, 2, GL_FLOAT, GL_FALSE, offsetof(TextVertex, position));
		glVertexArrayAttribBinding(VAO, 0, 0);

		glEnableVertexArrayAttrib(VAO, 1);
		glVertexArrayAttribFormat(VAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(TextVertex, uv));
		glVertexArrayAttribBinding(VAO, 1, 0);

		glEnableVertexArrayAttrib(VAO, 2);
		glVertexArrayAttribFormat(VAO, 2, 4, GL_FLOAT, GL_FALSE, offsetof(TextVertex, color));
		glVertexArrayAttribBinding(VAO, 2, 0);

		glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(TextVertex));

		return true;
	}

	void Destroy()
	{
		glMakeTextureHandleNonResidentARB(textureHandle);
		glDeleteProgram(program);
		glDeleteVertexArrays(1, &VAO);
		glDeleteBuffers(1, &VBO);
		glDeleteBuffers(1, &SSBO);
		glDeleteTextures(1, &texture);
	}

	void BeginFrame(int screenWidth, int screenHeight)
	{
		frameVerts.clear();
		frameProj = glm::ortho(0.0f, (float)screenWidth, (float)screenHeight, 0.0f, -1.0f, 1.0f);
	}

	void RenderText(const std::string& text, float x, float y, const glm::vec3& color)
	{
		glm::vec4 col(color, 1.0f);

		for (char c : text)
		{
			if (c < 32 || c >= 128) continue;

			stbtt_aligned_quad quad;
			stbtt_GetPackedQuad(charData, atlasWidth, atlasHeight, c - 32, &x, &y, &quad, 1);

			frameVerts.push_back({ {quad.x0, quad.y0}, {quad.s0, quad.t0}, col });
			frameVerts.push_back({ {quad.x1, quad.y0}, {quad.s1, quad.t0}, col });
			frameVerts.push_back({ {quad.x1, quad.y1}, {quad.s1, quad.t1}, col });

			frameVerts.push_back({ {quad.x0, quad.y0}, {quad.s0, quad.t0}, col });
			frameVerts.push_back({ {quad.x1, quad.y1}, {quad.s1, quad.t1}, col });
			frameVerts.push_back({ {quad.x0, quad.y1}, {quad.s0, quad.t1}, col });
		}
	}

	// Update the whole frame's accumulated glyph quads in one call
	void EndFrame()
	{
		if (frameVerts.empty()) return;

		size_t vertCount = frameVerts.size();
		size_t maxVerts = maxQuads * 6;
		if (vertCount > maxVerts)
		{
			std::cerr << "Text batch exceeds VBO capacity (" << vertCount << " > " << maxVerts << "), truncating.\n";
			vertCount = maxVerts;
		}

		// Update VBO
		glNamedBufferSubData(VBO, 0, vertCount * sizeof(TextVertex), frameVerts.data());

		// Update SSBO
		PerDrawTextData perDrawData{};
		perDrawData.proj = frameProj;
		perDrawData.textureHandle = textureHandle;
		glNamedBufferSubData(SSBO, 0, sizeof(perDrawData), &perDrawData);

		// Draw
		glUseProgram(program);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		glBindVertexArray(VAO);
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertCount);

		// Restore state
		glEnable(GL_CULL_FACE);
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);
	}
};

int main()
{
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW.\n";
		return EXIT_FAILURE;
	}
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Text", nullptr, nullptr);
	if (!window)
	{
		std::cerr << "Failed to create window.\n";
		glfwTerminate();
		return EXIT_FAILURE;
	}

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	glfwSetFramebufferSizeCallback(window, &FrameBufferSizeCallback);

	int version = gladLoadGL(glfwGetProcAddress);
	if (!version)
	{
		glfwDestroyWindow(window);
		glfwTerminate();
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

	glViewport(0, 0, sWidth, sHeight);

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(glDebugOutput, nullptr);

	// Enable depth testing
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	// Culling
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	// Data
	struct PerDrawData
	{
		glm::mat4 MVP;
	};

	struct Vertex
	{
		glm::vec3 position;
		glm::vec3 color;
	};

	const Vertex vertices[] = {
		// Pos                      // Color (RGB)
		{ { 0.0f,  0.5f,  0.0f},    {1.0f, 1.0f, 1.0f} }, // 0: Apex (White)
		{ {-0.5f, -0.5f,  0.5f},    {1.0f, 0.0f, 0.0f} }, // 1: Front-Left (Red)
		{ { 0.5f, -0.5f,  0.5f},    {0.0f, 1.0f, 0.0f} }, // 2: Front-Right (Green)
		{ { 0.5f, -0.5f, -0.5f},    {0.0f, 0.0f, 1.0f} }, // 3: Back-Right (Blue)
		{ {-0.5f, -0.5f, -0.5f},    {1.0f, 1.0f, 0.0f} }  // 4: Back-Left (Yellow)
	};

	const unsigned int indices[] = {
		// Base
		1, 4, 3,
		3, 2, 1,

		// Side Faces
		0, 1, 2,  // Front Face
		0, 2, 3,  // Right Face
		0, 3, 4,  // Back Face
		0, 4, 1   // Left Face
	};

	// Buffers
	GLuint VAO, VBO, EBO, SSBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(vertices), vertices, 0);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(indices), indices, 0);

	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(glm::mat4), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SSBO);

	// Attach buffers with VAO
	glCreateVertexArrays(1, &VAO);

	glEnableVertexArrayAttrib(VAO, 0);
	glVertexArrayAttribFormat(VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribBinding(VAO, 0, 0);

	glEnableVertexArrayAttrib(VAO, 1);
	glVertexArrayAttribFormat(VAO, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, color));
	glVertexArrayAttribBinding(VAO, 1, 0);

	glVertexArrayVertexBuffer(VAO, 0, VBO, 0, sizeof(Vertex));
	glVertexArrayElementBuffer(VAO, EBO);

	// Shader
	GLuint unlit = CreateShaderProgram(unlitVertex, unlitFrag);

	// Text renderer
	TextRenderer textRenderer;
	bool validText = textRenderer.Init("../../../resources/fonts/SpaceGrotesk-Regular.ttf");
	if (!validText)
	{
		std::cerr << "Text rendering disabled (font failed to load).\n";
	}

	double fpsTimer = glfwGetTime();
	int frameCount = 0;
	int fps = 0;

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		double time = glfwGetTime();
		// FPS calculation
		frameCount++;
		if (time - fpsTimer >= 1.0)
		{
			fps = frameCount / (int)(time - fpsTimer);
			frameCount = 0;
			fpsTimer = time;
		}

		glm::mat4 model = glm::rotate(glm::mat4(1.0f), (float)time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);
		glm::mat4 MVP = proj * view * model;

		PerDrawData perFrameData{};
		perFrameData.MVP = MVP;
		glNamedBufferSubData(SSBO, 0, sizeof(perFrameData), &perFrameData);

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(unlit);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, nullptr);

		// Text layer
		if (validText)
		{
			textRenderer.BeginFrame(sWidth, sHeight);

			std::string fpsText = std::format("FPS: {}", fps);
			textRenderer.RenderText(fpsText, 10.0f, 30.0f, glm::vec3(1.0f, 1.0f, 1.0f));
			textRenderer.RenderText("Backend: OpenGL 4.6 AZDO", 10.0f, 60.0f, glm::vec3(1.0f, 0.8f, 0.2f));

			textRenderer.EndFrame();
		}

		glfwSwapBuffers(window);
	}

	if (validText)
	{
		textRenderer.Destroy();
	}

	glDeleteProgram(unlit);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &SSBO);
	glfwDestroyWindow(window);
	glfwTerminate();

	return EXIT_SUCCESS;
}