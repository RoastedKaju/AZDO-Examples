#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>

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

	DestroySceneTarget(scene);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}
