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
#include <common.hpp>
#include <window_manager.hpp>
#include <shader_manager.hpp>
#include <file_manager.hpp>

struct Grid
{
	GLuint VAO;
	GLuint UBO;
};

struct CameraData
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
	glNamedBufferStorage(result.UBO, sizeof(CameraData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 1, result.UBO);

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
		-1, -1, -1, 1, -1, -1, 1, 1, -1, 1, 1, -1, -1, 1, -1, -1, -1, -1,
		-1, -1, 1, 1, -1, 1, 1, 1, 1, 1, 1, 1, -1, 1, 1, -1, -1, 1,
		-1, 1, 1, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, -1, 1, 1,
		1, 1, 1, 1, 1, -1, 1, -1, -1, 1, -1, -1, 1, -1, 1, 1, 1, 1,
		-1, -1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, -1, -1, 1, -1, -1, -1,
		-1, 1, -1, 1, 1, -1, 1, 1, 1, 1, 1, 1, -1, 1, 1, -1, 1, -1};

	glCreateVertexArrays(1, &result.VAO);
	glCreateBuffers(1, &result.VBO);
	glNamedBufferStorage(result.VBO, sizeof(vertices), vertices, 0);
	glVertexArrayVertexBuffer(result.VAO, 0, result.VBO, 0, 3 * sizeof(GLfloat));
	glEnableVertexArrayAttrib(result.VAO, 0);
	glVertexArrayAttribFormat(result.VAO, 0, 3, GL_FLOAT, GL_FALSE, 0);
	glVertexArrayAttribBinding(result.VAO, 0, 0);

	return result;
}

static GLuint LoadHDRI(const char *filePath)
{
	if (!std::filesystem::exists(filePath))
	{
		std::cerr << "HRDI file not found at path: " << filePath << std::endl;
		exit(EXIT_FAILURE);
	}

	int width, height, numChannels;
	stbi_set_flip_vertically_on_load(true);
	float *pixels = stbi_loadf(filePath, &width, &height, &numChannels, 3);
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
			{1.0f, 0.0f, 0.0f},	 // +X
			{-1.0f, 0.0f, 0.0f}, // -X
			{0.0f, 1.0f, 0.0f},	 // +Y
			{0.0f, -1.0f, 0.0f}, // -Y
			{0.0f, 0.0f, 1.0f},	 // +Z
			{0.0f, 0.0f, -1.0f}	 // -Z
	};

	const glm::vec3 ups[6] =
		{
			{0.0f, -1.0f, 0.0f}, // +X
			{0.0f, -1.0f, 0.0f}, // -X
			{0.0f, 0.0f, 1.0f},	 // +Y
			{0.0f, 0.0f, -1.0f}, // -Y
			{0.0f, -1.0f, 0.0f}, // +Z
			{0.0f, -1.0f, 0.0f}	 // -Z
	};

	// 90 degree projection
	glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

	// Shader setup
	glUseProgram(conversionProgram);
	GLint projectionLocation = glGetUniformLocation(conversionProgram, "projection");
	GLint viewLocation = glGetUniformLocation(conversionProgram, "view");
	// Get panorama handle
	GLuint64 panoramaHandle = glGetTextureHandleARB(panorama);
	glMakeTextureHandleResidentARB(panoramaHandle);

	// UBO to send panorama handle
	GLuint conversionUBO;
	glCreateBuffers(1, &conversionUBO);
	glNamedBufferStorage(conversionUBO, sizeof(GLuint64), &panoramaHandle, 0); // No need for dynamic sub-data just send it once
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, conversionUBO);
	// Send to shader
	glUniformMatrix4fv(projectionLocation, 1, GL_FALSE, &projection[0][0]);

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
	glDeleteBuffers(1, &conversionUBO);
	glMakeTextureHandleNonResidentARB(panoramaHandle);

	// Restore
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);
	glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

	return cubemap;
}

int main()
{
	WindowManager::Window window;
	window.Create("Grid", 800, 600);
	window.EnableOutputDebug();

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
	std::string cubeVS = FileManager::ReadTextFile("../../../resources/shaders/grid/cube.glsl");
	std::string convertFS = FileManager::ReadTextFile("../../../resources/shaders/grid/convert.glsl");
	GLuint conversionProgram = ShaderManager::CreateShaderProgram(cubeVS, convertFS);
	std::string skyboxFS = FileManager::ReadTextFile("../../../resources/shaders/grid/skybox.glsl");
	GLuint skyboxProgram = ShaderManager::CreateShaderProgram(cubeVS, skyboxFS);
	GLuint HDR = LoadHDRI("../../../resources/textures/sky.hdr");
	GLuint cubemap = PanoramaToCubemap(HDR, cubeMesh.VAO, conversionProgram, 512); // Size should be width of texture / 4
	// you can now delete the HDR texture
	glDeleteTextures(1, &HDR);

	// Get cubemap's texture handle
	GLuint64 cubemapHandle = glGetTextureHandleARB(cubemap);
	glMakeTextureHandleResidentARB(cubemapHandle);
	// Make UBO and send this handle to GPU
	GLuint cubemapUBO;
	glCreateBuffers(1, &cubemapUBO);
	glNamedBufferStorage(cubemapUBO, sizeof(GLuint64), &cubemapHandle, 0); // No need for dynamic sub-data, just send the handle once
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, cubemapUBO);					   // index 1 will be camera UBO for Grid

	// Cache locations
	const GLint projectionLocation = glGetUniformLocation(skyboxProgram, "projection");
	const GLint viewLocation = glGetUniformLocation(skyboxProgram, "view");

	// Grid
	Grid grid = MakeGrid();
	std::string gridVS = FileManager::ReadTextFile("../../../resources/shaders/grid/grid_vertex.glsl");
	std::string gridFS = FileManager::ReadTextFile("../../../resources/shaders/grid/grid_fragment.glsl");
	GLuint gridProgram = ShaderManager::CreateShaderProgram(gridVS, gridFS);

	while (!window.ShouldClose())
	{
		if (window.ProcessEvents() == false)
		{
			continue;
		}

		auto windowExtent = window.GetSize();

		float time = (float)glfwGetTime();
		float yaw = time * 0.12f;

		glm::vec3 eye(0.0f, 2.0f, 5.0f);
		glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
		glm::vec3 target = eye + forward;
		glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)windowExtent.width / (float)windowExtent.height, 0.1f, 100.0f);
		glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 inverseViewProjection = inverse(projection * view);

		CameraData cameraData{
			.uView = view,
			.uProjection = projection,
			.uInvViewProjection = inverseViewProjection,
			.uCameraPosition = glm::vec4(eye, 1.0f)};
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

		window.SwapBuffers();
	}

	// Cleanup
	glDeleteProgram(conversionProgram);
	glDeleteProgram(skyboxProgram);
	glMakeTextureHandleNonResidentARB(cubemapHandle);
	glDeleteTextures(1, &cubemap);
	glDeleteVertexArrays(1, &cubeMesh.VAO);
	glDeleteBuffers(1, &cubeMesh.VBO);
	glDeleteBuffers(1, &cubemapUBO);
	glDeleteVertexArrays(1, &grid.VAO);
	glDeleteBuffers(1, &grid.UBO);

	window.Destroy();
	return EXIT_SUCCESS;
}