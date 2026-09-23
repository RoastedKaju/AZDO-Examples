#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <filesystem>

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

static constexpr const char* cubeVS = R"(
#version 460 core

layout(location=0) in vec3 position;
layout(location=0) out vec3 direction;

uniform mat4 projection;
uniform mat4 view;

void main() {
	direction = position;
	gl_Position = projection * view * vec4(position,1.0);
}
)";

static constexpr const char* convertFS = R"(
#version 460 core

layout(location=0) in vec3 direction;
layout(location=0) out vec4 color;

layout(binding=0) uniform sampler2D panorama;

vec2 uvFromDirection(vec3 d) {
	d = normalize(d);
	return vec2(atan(d.z,d.x)*0.15915494309+0.5, asin(clamp(d.y,-1.,1.))*0.31830988618+0.5);
}

void main() {
	color = vec4(texture(panorama,uvFromDirection(direction)).rgb,1.0);
}
)";

static constexpr const char* skyboxFS = R"(
#version 460 core

in vec3 direction;

layout(location=0) out vec4 color;

layout(binding=0) uniform samplerCube environmentMap;

void main() {
	vec3 hdr = texture(environmentMap, normalize(direction)).rgb;

	float exposure = 1.0;
	vec3 mapped = vec3(1.0) - exp(-hdr * exposure);
	mapped = pow(mapped, vec3(1.0 / 2.2));

	color = vec4(mapped, 1.0);
}
)";

const char* gridVS = R"(
#version 460 core

layout(std140, binding = 0) uniform Camera
{
    mat4 uView;
    mat4 uProjection;
    mat4 uInvViewProjection; // inverse(uProjection * uView)
    vec4 uCameraPosition;
};

layout (location = 0) out vec3 vNearPoint;
layout (location = 1) out vec3 vFarPoint;

const vec2 kFullscreenQuad[6] = vec2[6](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
);

vec3 unproject(vec2 ndcXY, float ndcZ)
{
    vec4 p = uInvViewProjection * vec4(ndcXY, ndcZ, 1.0);
    return p.xyz / p.w;
}

void main()
{
    vec2 ndc = kFullscreenQuad[gl_VertexID];

    // Default OpenGL depth convention: NDC z is [-1, 1].
    vNearPoint = unproject(ndc, -1.0);
    vFarPoint  = unproject(ndc,  1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
})";

const char* gridFS = R"(
#version 460 core

layout(std140, binding = 0) uniform Camera
{
    mat4 uView;
    mat4 uProjection;
    mat4 uInvViewProjection;
    vec4 uCameraPosition;
};

layout (location = 0) in vec3 vNearPoint;
layout (location = 1) in vec3 vFarPoint;

layout(location = 0) out vec4 outColor;

float gridLine(vec2 worldXZ, float spacing)
{
    vec2 coord = worldXZ / spacing;
    vec2 derivative = fwidth(coord);

    // Distance to nearest grid line, expressed in pixels.
    vec2 line = abs(fract(coord - 0.5) - 0.5) / derivative;
    return 1.0 - min(min(line.x, line.y), 1.0);
}

void main()
{
    vec3 ray = vFarPoint - vNearPoint;

    // Intersection with y = 0 (the XZ grid plane).
    float t = -vNearPoint.y / ray.y;
    if (t <= 0.0)
        discard;

    vec3 worldPos = vNearPoint + t * ray;

    // Discard intersections beyond the camera far plane.
    vec4 clip = uProjection * uView * vec4(worldPos, 1.0);
    float ndcDepth = clip.z / clip.w;
    if (ndcDepth < -1.0 || ndcDepth > 1.0)
        discard;

    // Convert OpenGL's default NDC depth [-1, 1] to window depth [0, 1].
    gl_FragDepth = ndcDepth * 0.5 + 0.5;

    float minor = gridLine(worldPos.xz, 1.0);
    float major = gridLine(worldPos.xz, 10.0);

    float xAxis = 1.0 - smoothstep(0.0, fwidth(worldPos.z) * 1.5, abs(worldPos.z));
    float zAxis = 1.0 - smoothstep(0.0, fwidth(worldPos.x) * 1.5, abs(worldPos.x));

    vec3 minorColor = vec3(0.50, 0.50, 0.50);
    vec3 majorColor = vec3(0.20, 0.20, 0.20);
    vec3 xAxisColor = vec3(1.00, 0.10, 0.10);
    vec3 zAxisColor = vec3(0.10, 0.10, 1.00);

    vec3 color = mix(minorColor, majorColor, major);
    color = mix(color, xAxisColor, xAxis); // X axis
    color = mix(color, zAxisColor, zAxis); // Z axis

    float distanceToCamera = length(worldPos.xz - uCameraPosition.xz);

    // Controls where the fading starts and become completely invisible
    float fade = 1.0 - smoothstep(20.0, 40.0, distanceToCamera);
    float alpha = max(max(minor, major), max(xAxis, zAxis)) * fade;

    if (alpha < 0.002)
        discard;

    outColor = vec4(color, alpha);
}
)";

struct Grid
{
	GLuint VAO;
	GLuint UBO;
};

struct CamerData
{
	glm::mat4 uView;
	glm::mat4 uProjection;
	glm::mat4 uInvViewProjection;
	glm::vec4 uCameraPosition;
};

static Grid MakeGrid()
{
	Grid result{};

	glCreateVertexArrays(1, &result.VAO);
	// No need for a VBO or EBO, the data is hard-coded in shaders

	glCreateBuffers(1, &result.UBO);
	glNamedBufferStorage(result.UBO, sizeof(CamerData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, result.UBO);

	return result;
}

struct CubeMesh
{
	GLuint VAO;
	GLuint VBO;
};

static CubeMesh MakeCube()
{
	CubeMesh result{};

	static const float vertices[] = {
	  -1,-1,-1,  1,-1,-1,  1, 1,-1,  1, 1,-1, -1, 1,-1, -1,-1,-1,
	  -1,-1, 1,  1,-1, 1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1,-1, 1,
	  -1, 1, 1, -1, 1,-1, -1,-1,-1, -1,-1,-1, -1,-1, 1, -1, 1, 1,
	   1, 1, 1,  1, 1,-1,  1,-1,-1,  1,-1,-1,  1,-1, 1,  1, 1, 1,
	  -1,-1,-1,  1,-1,-1,  1,-1, 1,  1,-1, 1, -1,-1, 1, -1,-1,-1,
	  -1, 1,-1,  1, 1,-1,  1, 1, 1,  1, 1, 1, -1, 1, 1, -1, 1,-1 };

	glCreateVertexArrays(1, &result.VAO);
	glCreateBuffers(1, &result.VBO);
	glNamedBufferStorage(result.VBO, sizeof(vertices), vertices, 0);
	glVertexArrayVertexBuffer(result.VAO, 0, result.VBO, 0, 3 * sizeof(GLfloat));
	glEnableVertexArrayAttrib(result.VAO, 0);
	glVertexArrayAttribFormat(result.VAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(result.VAO, 0, 0);

	return result;
}

static GLuint LoadHDRI(const char* filePath)
{
	if (!std::filesystem::exists(filePath))
	{
		std::cerr << "HRDI file not found at path: " << filePath << std::endl;
		exit(EXIT_FAILURE);
	}

	int width, height, numChannels;
	stbi_set_flip_vertically_on_load(true);
	float* pixels = stbi_loadf(filePath, &width, &height, &numChannels, 3);
	if (!pixels)
	{
		std::cerr << "Invalid data inside HRDI image: " << filePath << std::endl;
		exit(EXIT_FAILURE);
	}

	// Create texture
	GLuint texture;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);
	glTextureStorage2D(texture, 1, GL_RGB16F, width, height);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGB, GL_FLOAT, pixels);
	glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	stbi_image_free(pixels);

	return texture;
}

static GLuint PanoramaToCubemap(GLuint panorama, GLuint cubeVAO, GLuint conversionProgram, int size)
{
	// Save/override state for the off-screen conversion.
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);
	// save caller's viewport
	GLint prevViewport[4];
	glGetIntegerv(GL_VIEWPORT, prevViewport);

	GLuint cubemap;
	glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &cubemap);
	glTextureStorage2D(cubemap, 1, GL_RGB16F, size, size);
	glTextureParameteri(cubemap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(cubemap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTextureParameteri(cubemap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(cubemap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(cubemap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	// Frame buffer object for our off-screen cubemap generation
	GLuint FBO;
	glCreateFramebuffers(1, &FBO);

	const glm::vec3 directions[6] =
	{
		{ 1.0f,  0.0f,  0.0f}, // +X
		{-1.0f,  0.0f,  0.0f}, // -X
		{ 0.0f,  1.0f,  0.0f}, // +Y
		{ 0.0f, -1.0f,  0.0f}, // -Y
		{ 0.0f,  0.0f,  1.0f}, // +Z
		{ 0.0f,  0.0f, -1.0f}  // -Z
	};

	const glm::vec3 ups[6] =
	{
		{ 0.0f, -1.0f,  0.0f}, // +X
		{ 0.0f, -1.0f,  0.0f}, // -X
		{ 0.0f,  0.0f,  1.0f}, // +Y
		{ 0.0f,  0.0f, -1.0f}, // -Y
		{ 0.0f, -1.0f,  0.0f}, // +Z
		{ 0.0f, -1.0f, 0.0f}   // -Z
	};

	// 90 degree projection
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

	// Shader setup
	glUseProgram(conversionProgram);
	GLint projectionLocation = glGetUniformLocation(conversionProgram, "projection");
	GLint viewLocation = glGetUniformLocation(conversionProgram, "view");

	glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, &projection[0][0]);

	glBindTextureUnit(0, panorama);

	glViewport(0, 0, size, size);
	glBindFramebuffer(GL_FRAMEBUFFER, FBO);

	// Render each cubemap face
	for (int face = 0; face < 6; ++face)
	{
		glNamedFramebufferTextureLayer(FBO, GL_COLOR_ATTACHMENT0, cubemap, 0, face);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			std::cerr << "Cubemap framebuffer incomplete." << std::endl;
			exit(EXIT_FAILURE);
		}

		glm::mat4 view = glm::lookAt(glm::vec3(0.0f), directions[face], ups[face]);

		glUniformMatrix4fv(viewLocation, 1, GL_FALSE, &view[0][0]);

		glClear(GL_COLOR_BUFFER_BIT);

		glBindVertexArray(cubeVAO);
		glDrawArrays(GL_TRIANGLES, 0, 36);
	}

	// Cleanup
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDeleteFramebuffers(1, &FBO);
	//glDeleteRenderbuffers(1, &depth);

	// Restore
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

	return cubemap;
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

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Grid", nullptr, nullptr);
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
	// Cubemap state
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	CubeMesh cubeMesh = MakeCube();
	GLuint conversionProgram = CreateShaderProgram(cubeVS, convertFS);
	GLuint skyboxProgram = CreateShaderProgram(cubeVS, skyboxFS);
	GLuint HDR = LoadHDRI("../../../resources/textures/sky.hdr");
	GLuint cubemap = PanoramaToCubemap(HDR, cubeMesh.VAO, conversionProgram, 512); // Size should be width of texture / 4
	// you can now delete the HDR texture
	glDeleteTextures(1, &HDR);

	// Cache location
	const GLint projectionLocation = glGetUniformLocation(skyboxProgram, "projection");
	const GLint viewLocation = glGetUniformLocation(skyboxProgram, "view");

	// Grid
	Grid grid = MakeGrid();
	GLuint gridProgram = CreateShaderProgram(gridVS, gridFS);

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (sWidth == 0 || sHeight == 0)
		{
			glfwWaitEvents();
			continue;
		}

		float time = (float)glfwGetTime();
		float yaw = time * 0.12f;

		glm::vec3 eye(0.0f, 2.0f, 5.0f);
		glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
		glm::vec3 target = eye + forward;
		glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);
		glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 inverseViewProjection = inverse(projection * view);

		CamerData cameraData
		{
			.uView = view,
			.uProjection = projection,
			.uInvViewProjection = inverseViewProjection,
			.uCameraPosition = glm::vec4(eye, 1.0f)
		};
		// Send UBO to shader
		glNamedBufferSubData(grid.UBO, 0, sizeof(cameraData), &cameraData);

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render opaque scene objects here first.

		// Render skybox: only fill pixels still at far depth.
		glm::mat4 skyboxView = glm::mat4(glm::mat3(view));

		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		glDisable(GL_CULL_FACE); // Or cull front faces for an inward cube.

		glUseProgram(skyboxProgram);
		glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, &projection[0][0]);
		glUniformMatrix4fv(viewLocation, 1, GL_FALSE, &skyboxView[0][0]);
		glBindTextureUnit(0, cubemap);
		glBindVertexArray(cubeMesh.VAO);
		glDrawArrays(GL_TRIANGLES, 0, 36);

		// Render grid over skybox, but behind opaque objects.
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthFunc(GL_LEQUAL);
		glDepthMask(GL_FALSE);

		glUseProgram(gridProgram);
		glBindVertexArray(grid.VAO);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		glDisable(GL_BLEND);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
		glEnable(GL_CULL_FACE);

		glfwSwapBuffers(window);
	}

	// Cleanup
	glDeleteProgram(conversionProgram);
	glDeleteProgram(skyboxProgram);
	glDeleteTextures(1, &cubemap);
	glDeleteVertexArrays(1, &cubeMesh.VAO);
	glDeleteBuffers(1, &cubeMesh.VBO);
	glDeleteVertexArrays(1, &grid.VAO);
	glDeleteBuffers(1, &grid.UBO);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}