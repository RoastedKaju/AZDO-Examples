#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stb_image.h>
#include <iostream>
#include <vector>
#include <string>

static int sWidth = 800;
static int sHeight = 600;
static bool sResized = false;

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
	sResized = true;
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

static bool LoadImage(const char* path, std::vector<char>& outPixels, int& outWidth, int& outHeight)
{
	int width = 0, height = 0, sourceChannels = 0;
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

// Post processing shaders
const char* postProcessVertex = R"(
#version 460 core

layout (location = 0) out vec2 uv;

vec2 positions[3] = {
	vec2(-1.0, -1.0),
	vec2( 3.0, -1.0),
	vec2(-1.0,  3.0)
};

void main() {
	vec2 pos = positions[gl_VertexID];
	uv = pos * 0.5 + 0.5;
	gl_Position = vec4(pos, 0.0, 1.0);
}
)";

const char* postProcessFrag = R"(
#version 460 core
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 FragColor;

layout (std430, binding = 1) readonly buffer PerDrawData {
	uvec2 uSceneTextureHandle;
};

void main() {
	sampler2D sceneTex = sampler2D(uSceneTextureHandle);
	vec3 sceneColor = texture(sceneTex, uv).rgb;

	// Apply gamma correction
	sceneColor = pow(sceneColor, vec3(1.0 / 2.2));

	FragColor = vec4(sceneColor, 1.0);
}
)";

const char* thumbnailVertShader = R"(
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

const char* thumbnailFragShader = R"(
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

const Vertex pyramidVertices[] = {
	// Pos                      // Color (RGB)
	{ { 0.0f,  0.5f,  0.0f},    {1.0f, 1.0f, 1.0f} }, // 0: Apex (White)
	{ {-0.5f, -0.5f,  0.5f},    {1.0f, 0.0f, 0.0f} }, // 1: Front-Left (Red)
	{ { 0.5f, -0.5f,  0.5f},    {0.0f, 1.0f, 0.0f} }, // 2: Front-Right (Green)
	{ { 0.5f, -0.5f, -0.5f},    {0.0f, 0.0f, 1.0f} }, // 3: Back-Right (Blue)
	{ {-0.5f, -0.5f, -0.5f},    {1.0f, 1.0f, 0.0f} }  // 4: Back-Left (Yellow)
};

const unsigned int pyramidIndices[] = {
	// Base
	1, 4, 3,
	3, 2, 1,

	// Side Faces
	0, 1, 2,  // Front Face
	0, 2, 3,  // Right Face
	0, 3, 4,  // Back Face
	0, 4, 1   // Left Face
};

// Scene render target
struct SceneTarget
{
	GLuint FBO = 0;
	GLuint color = 0;
	GLuint depth = 0;
	GLuint64 colorHandle = 0;
};

static SceneTarget CreateSceneTarget(int width, int height)
{
	SceneTarget target;

	glCreateFramebuffers(1, &target.FBO);

	// Color
	glCreateTextures(GL_TEXTURE_2D, 1, &target.color);
	glTextureStorage2D(target.color, 1, GL_RGBA8, width, height);
	// After glGetTextureHandleARB the texture's sampling parameters are immutable.
	glTextureParameteri(target.color, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(target.color, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(target.color, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(target.color, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// Attach scene color with your FBO
	glNamedFramebufferTexture(target.FBO, GL_COLOR_ATTACHMENT0, target.color, 0);

	target.colorHandle = glGetTextureHandleARB(target.color);
	glMakeTextureHandleResidentARB(target.colorHandle);

	// Depth
	glCreateTextures(GL_TEXTURE_2D, 1, &target.depth);
	glTextureStorage2D(target.depth, 1, GL_DEPTH24_STENCIL8, width, height);
	glNamedFramebufferTexture(target.FBO, GL_DEPTH_STENCIL_ATTACHMENT, target.depth, 0);

	if (glCheckNamedFramebufferStatus(target.FBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		std::cerr << "FBO incomplete.\n";
		exit(EXIT_FAILURE);
	}

	return target;
}

static void DestroySceneTarget(SceneTarget& target)
{
	if (target.colorHandle)
	{
		glMakeTextureHandleNonResidentARB(target.colorHandle);
		target.colorHandle = 0;
	}
	if (target.FBO) { glDeleteFramebuffers(1, &target.FBO);	target.FBO = 0; }
	if (target.color) { glDeleteTextures(1, &target.color);	target.color = 0; }
	if (target.depth) { glDeleteTextures(1, &target.depth);	target.depth = 0; }
}

struct QuadVertex
{
	glm::vec3 position;
	glm::vec2 texCoord;
};

QuadVertex quadVertices[] = {
	{ { -1.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
	{ {  1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
	{ {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f } },
	{ { -1.0f,  1.0f, 0.0f }, { 0.0f, 0.0f } }
};

GLuint quadIndices[] = {
	0, 1, 2,
	2, 3, 0
};

struct PerDrawDataThumbnail
{
	glm::mat4 MVP;
	GLuint64 textureHandle;
};

GLuint quadVAO, quadVBO, quadEBO, quadSSBO;

static void GenerateQuad()
{
	glCreateBuffers(1, &quadVBO);
	glNamedBufferStorage(quadVBO, sizeof(quadVertices), quadVertices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &quadEBO);
	glNamedBufferStorage(quadEBO, sizeof(quadIndices), quadIndices, GL_DYNAMIC_STORAGE_BIT);

	glCreateBuffers(1, &quadSSBO);
	glNamedBufferStorage(quadSSBO, sizeof(PerDrawDataThumbnail), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, quadSSBO); // can use index 0 here as this will happen only once at start and cleaned-up

	// VAO
	glCreateVertexArrays(1, &quadVAO);

	glEnableVertexArrayAttrib(quadVAO, 0);
	glVertexArrayAttribFormat(quadVAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, position));
	glVertexArrayAttribBinding(quadVAO, 0, 0);

	glEnableVertexArrayAttrib(quadVAO, 1);
	glVertexArrayAttribFormat(quadVAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(QuadVertex, texCoord));
	glVertexArrayAttribBinding(quadVAO, 1, 0);

	glVertexArrayVertexBuffer(quadVAO, 0, quadVBO, 0, sizeof(QuadVertex)); // attach VBO to VAO
	glVertexArrayElementBuffer(quadVAO, quadEBO); // attach EBO to VAO
}

std::vector<char> pixels;
int debugImgWidth;
int debugImgheight;
GLuint debugTexture;
GLuint64 debugtextureHandle;

static void LoadDebugTexture()
{
	if (!LoadImage("../../../resources/textures/uv_tiles.png", pixels, debugImgWidth, debugImgheight))
	{
		exit(EXIT_FAILURE);
	}
	// Create texture
	glCreateTextures(GL_TEXTURE_2D, 1, &debugTexture);
	GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(debugImgWidth, debugImgheight)))) + 1;
	glTextureStorage2D(debugTexture, levels, GL_RGBA8, debugImgWidth, debugImgheight);
	glTextureSubImage2D(debugTexture, 0, 0, 0, debugImgWidth, debugImgheight, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	// Enable mipmapping and filtering
	glGenerateTextureMipmap(debugTexture);
	glTextureParameteri(debugTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(debugTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(debugTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTextureParameteri(debugTexture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// Bindless texture
	debugtextureHandle = glGetTextureHandleARB(debugTexture);
	glMakeTextureHandleResidentARB(debugtextureHandle);
}

GLuint thumbnailShaderPrg;
SceneTarget thumbnailTarget;
static void RenderThumbnail()
{
	const int thumbnailSize = 512;

	thumbnailShaderPrg = CreateShaderProgram(thumbnailVertShader, thumbnailFragShader);

	glm::mat4 model = glm::mat4(1.0f);
	glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)thumbnailSize / (float)thumbnailSize, 0.1f, 100.0f);
	glm::mat4 mvp = proj * view * model;

	PerDrawDataThumbnail frameData{};
	frameData.MVP = mvp;
	frameData.textureHandle = debugtextureHandle;
	// Send to GPU
	glNamedBufferSubData(quadSSBO, 0, sizeof(PerDrawDataThumbnail), &frameData);

	// Render target
	thumbnailTarget = CreateSceneTarget(thumbnailSize, thumbnailSize);
	glBindFramebuffer(GL_FRAMEBUFFER, thumbnailTarget.FBO);
	glViewport(0, 0, thumbnailSize, thumbnailSize);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.5f, 0.12f, 0.5f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glUseProgram(thumbnailShaderPrg);
	glBindVertexArray(quadVAO);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

	// cleanup
	glDeleteProgram(thumbnailShaderPrg);
	glDeleteVertexArrays(1, &quadVAO);
	glDeleteBuffers(1, &quadVBO);
	glDeleteBuffers(1, &quadEBO);
	glDeleteBuffers(1, &quadSSBO);
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

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Framebuffers", nullptr, nullptr);
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

	// Use the real frame-buffer size (differs from window size on HiDPI displays)
	glfwGetFramebufferSize(window, &sWidth, &sHeight);
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

	// Thumbnail generation
	// The idea is to render one frame of mesh and delete it, use the resulting texture in our UI
	GenerateQuad();
	LoadDebugTexture();
	RenderThumbnail();

	// Buffers
	GLuint VAO, VBO, EBO, SSBO;

	glCreateBuffers(1, &VBO);
	glNamedBufferStorage(VBO, sizeof(pyramidVertices), pyramidVertices, 0);

	glCreateBuffers(1, &EBO);
	glNamedBufferStorage(EBO, sizeof(pyramidIndices), pyramidIndices, 0);

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

	// Full screen VAO (OpenGL still requires a VAO)
	GLuint postProcessVAO;
	GLuint postProcessSSBO;
	glCreateVertexArrays(1, &postProcessVAO);
	// No need for a VBO or EBO
	glCreateBuffers(1, &postProcessSSBO);
	glNamedBufferStorage(postProcessSSBO, sizeof(GLuint64), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, postProcessSSBO); // Index 1 here, 0 is for unlit shaders

	// Scene frame buffer
	// This is where we will be rendering our pyramid scene to
	SceneTarget scene = CreateSceneTarget(sWidth, sHeight);
	// The handle only changes when the target is recreated, so upload it here
	glNamedBufferSubData(postProcessSSBO, 0, sizeof(GLuint64), &scene.colorHandle);

	// Shader
	GLuint unlit = CreateShaderProgram(unlitVertex, unlitFrag);
	GLuint postProcess = CreateShaderProgram(postProcessVertex, postProcessFrag);

	// Enable ImGUI
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 460");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (sWidth == 0 || sHeight == 0)
		{
			glfwWaitEvents();
			continue;
		}

		// Recreate size-dependent resources after a resize
		if (sResized)
		{
			sResized = false;

			DestroySceneTarget(scene);
			scene = CreateSceneTarget(sWidth, sHeight);

			// New texture -> new handle, so update what the shader reads
			glNamedBufferSubData(postProcessSSBO, 0, sizeof(GLuint64), &scene.colorHandle);
		}

		float time = (float)glfwGetTime();

		glm::mat4 model = glm::rotate(glm::mat4(1.0f), (float)time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);
		glm::mat4 MVP = proj * view * model;

		PerDrawData perFrameData{};
		perFrameData.MVP = MVP;
		glNamedBufferSubData(SSBO, 0, sizeof(perFrameData), &perFrameData);

		// Pyramid scene pass
		glBindFramebuffer(GL_FRAMEBUFFER, scene.FBO);
		glViewport(0, 0, sWidth, sHeight); // Each pass should natually have its own viewport, this is good in case you want half-res bloom pass later on
		glEnable(GL_DEPTH_TEST);
		// Clear
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		// Render pyramid
		glUseProgram(unlit);
		glBindVertexArray(VAO);
		glDrawElements(GL_TRIANGLES, 18, GL_UNSIGNED_INT, nullptr);

		// Post-Process pass
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, sWidth, sHeight);
		glDisable(GL_DEPTH_TEST);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		// Render Full-screen triangle
		glUseProgram(postProcess);
		glBindVertexArray(postProcessVAO);
		glDrawArrays(GL_TRIANGLES, 0, 3);

		// ImGUI-Pass
		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		ImGui::Begin("Thumbnail");
		ImVec2 availableSizeThumbnail = ImGui::GetContentRegionAvail();
		ImGui::Image((ImTextureID)(intptr_t)thumbnailTarget.color, availableSizeThumbnail, ImVec2(0, 1), ImVec2(1, 0));
		ImGui::End();

		ImGui::Begin("Render Target");
		ImVec2 availableSize = ImGui::GetContentRegionAvail();
		// With the OpenGL3 ImGui backend, the texture ID is normally passed as a pointer-sized value
		// Also flip the result image
		ImGui::Image((ImTextureID)(intptr_t)scene.color, availableSize, ImVec2(0, 1), ImVec2(1, 0));
		ImGui::End();

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		glfwSwapBuffers(window);
	}

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glDeleteProgram(unlit);
	glDeleteVertexArrays(1, &VAO);
	glDeleteBuffers(1, &VBO);
	glDeleteBuffers(1, &EBO);
	glDeleteBuffers(1, &SSBO);
	DestroySceneTarget(scene);
	// Destroy thumbnail target
	DestroySceneTarget(thumbnailTarget);
	// GLFW
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}
