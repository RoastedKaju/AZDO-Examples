#include <glad/gl.h>
#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include <file_manager.hpp>
#include <shader_manager.hpp>
#include <window_manager.hpp>
#include <gl_manager.hpp>
#include <gltf_importer.hpp>

struct Vertex
{
	glm::vec3 position;
	glm::vec2 texCoord;
};

struct PerFrameData
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 projection;
	GLuint64 baseColorHandle;
};

struct DrawItem
{
	GLsizei indexCount;
	GLuint firstIndex;  // offset into the shared EBO, in indices
	GLint baseVertex;   // offset into the shared VBO, in vertices
	GLuint64 baseColorHandle;
};

struct Instance
{
	glm::mat4 world;
	DrawItem draw;
};

GlTFModelData modelData{};

int main()
{
	// create window
	WindowManager::Window window{};
	window.CreateWindow("Importer", 800, 600);
	window.EnableOutputDebug();

	GlDevice::DefaultTextureSet defaultTextureSet{};
	defaultTextureSet.Initialize();

	// load model
	if (!GlTFImporter::LoadGlTF("../../../resources/models/heather.glb", modelData))
	{
		return EXIT_FAILURE;
	}

	GlTFImporter::PrintGlTFSummary(modelData);

	// load shaders
	const std::string vs = FileManager::ReadTextFile("../../../resources/shaders/importer/vertex.glsl");
	const std::string fs = FileManager::ReadTextFile("../../../resources/shaders/importer/fragment.glsl");
	GLuint shaderProgram = ShaderManager::CreateShaderProgram(vs, fs);
	if (!shaderProgram)
	{
		return EXIT_FAILURE;
	}

	// Upload images to GPU
	std::vector<GLuint64> gpuTextureHandles(modelData.images.size(), 0);
	for (size_t i = 0; i < modelData.images.size(); ++i)
	{
		const auto& img = modelData.images[i];
		if (img.pixels.empty()) continue;

		GLuint tex = 0;
		glCreateTextures(GL_TEXTURE_2D, 1, &tex);
		glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		// Upload pixel data (assume RGBA8; adjust format/type based on img.channels)
		GLenum format = (img.channels == 4) ? GL_RGBA : GL_RGB;
		glTextureStorage2D(tex, 1, GL_RGBA8, img.width, img.height);
		glTextureSubImage2D(tex, 0, 0, 0, img.width, img.height, format, GL_UNSIGNED_BYTE, img.pixels.data());
		glGenerateTextureMipmap(tex);

		// Get bindless handle and make resident
		GLuint64 handle = glGetTextureHandleARB(tex);
		glMakeTextureHandleResidentARB(handle);

		gpuTextureHandles[i] = handle;
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	// [mesh][primitive]
	std::vector<std::vector<DrawItem>> meshDraws(modelData.meshes.size());

	for (size_t i = 0; i < modelData.meshes.size(); ++i)
	{
		for (const auto& primitive : modelData.meshes[i].primitives)
		{
			GLuint64 baseColorHandle = defaultTextureSet.baseColorHandle;
			if (primitive.materialIndex >= 0 && primitive.materialIndex < static_cast<int>(modelData.materials.size()))
			{
				int imgIdx = modelData.materials[primitive.materialIndex].baseColorTextureIdx;
				if (imgIdx >= 0 && imgIdx < static_cast<int>(gpuTextureHandles.size())) { baseColorHandle = gpuTextureHandles[imgIdx]; }
			}

			meshDraws[i].push_back({ static_cast<GLsizei>(primitive.indices.size()),
									 static_cast<GLuint>(indices.size()),
									 static_cast<GLint>(vertices.size()),
									 baseColorHandle });

			for (const auto& v : primitive.vertices)
			{
				vertices.push_back({ v.position, v.texCoord });
			}
			indices.insert(indices.end(), primitive.indices.begin(), primitive.indices.end());
		}
	}
	if (vertices.empty() || indices.empty())
	{
		std::cerr << "Model contains no drawable geometry." << std::endl;
		return EXIT_FAILURE;
	}

	std::vector<Instance> instances;
	std::vector<std::pair<int, glm::mat4>> stack;
	for (int root : modelData.rootNodes) stack.push_back({ root, glm::mat4(1.0f) });

	while (!stack.empty())
	{
		const auto [nodeIdx, parentWorld] = stack.back();
		stack.pop_back();
		if (nodeIdx < 0 || nodeIdx >= static_cast<int>(modelData.nodes.size())) continue;

		const GlTFModelData::Node& node = modelData.nodes[nodeIdx];
		const glm::mat4 world = parentWorld * node.localTransform;

		if (node.meshIndex >= 0)
		{
			for (const DrawItem& draw : meshDraws[node.meshIndex])
			{
				instances.push_back({ world, draw });
			}
		}
		for (int child : node.childrenIndices) stack.push_back({ child, world });
	}
	if (instances.empty())
	{
		std::cerr << "No mesh instances found in the scene." << std::endl;
		return EXIT_FAILURE;
	}

	// normalize model scale
	//glm::vec3 sceneMin(FLT_MAX), sceneMax(-FLT_MAX);
	//float maxExtent = glm::compMax(sceneMax - sceneMin);
	//float scaleFactor = 2.0f / maxExtent; // Fit within 2-unit box

	//for (Instance& instance : instances)
	//{
	//	instance.world = glm::scale(glm::mat4(1.0f), glm::vec3(scaleFactor)) * instance.world;
	//}

	GLuint vao = 0, vbo = 0, ebo = 0, ubo = 0;
	glCreateVertexArrays(1, &vao);

	glCreateBuffers(1, &vbo);
	glNamedBufferStorage(vbo, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), 0);

	glCreateBuffers(1, &ebo);
	glNamedBufferStorage(ebo, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)), indices.data(), 0);

	// vertex layout: location 0 = position, location 1 = texCoord, both from buffer binding 0
	glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
	glVertexArrayElementBuffer(vao, ebo);

	glEnableVertexArrayAttrib(vao, 0);
	glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
	glVertexArrayAttribBinding(vao, 0, 0);

	glEnableVertexArrayAttrib(vao, 1);
	glVertexArrayAttribFormat(vao, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
	glVertexArrayAttribBinding(vao, 1, 0);

	// UBO with M, V, P, bound to binding point 0 (matches `binding = 0` in the vertex shader)
	glCreateBuffers(1, &ubo);
	glNamedBufferStorage(ubo, sizeof(PerFrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

	while (!window.ShouldClose())
	{
		if (!window.ProcessEvents())
		{
			continue;
		}

		float time = (float)glfwGetTime();

		const Extent2D windowSize = window.GetSize();
		const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::mat4 projection = glm::perspective(glm::radians(45.0f), static_cast<float>(windowSize.width) / static_cast<float>(windowSize.height), 0.1f, 100.0f);

		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glUseProgram(shaderProgram);
		glBindVertexArray(vao);

		for (const Instance& instance : instances)
		{
			glm::mat4 model = instance.world;
			model = glm::scale(model, glm::vec3(100));
			PerFrameData matrices{ model, view, projection, instance.draw.baseColorHandle };
			glNamedBufferSubData(ubo, 0, sizeof(PerFrameData), &matrices);
			glDrawElementsBaseVertex(GL_TRIANGLES, instance.draw.indexCount, GL_UNSIGNED_INT, reinterpret_cast<const void*>(static_cast<uintptr_t>(instance.draw.firstIndex) * sizeof(uint32_t)), instance.draw.baseVertex);
		}

		window.SwapBuffers();
	}

	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo);
	glDeleteBuffers(1, &ebo);
	glDeleteBuffers(1, &ubo);
	glDeleteProgram(shaderProgram);

	defaultTextureSet.Destroy();
	window.Destroy();

	return EXIT_SUCCESS;
}