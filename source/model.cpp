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
	glm::vec2 texCoord;
};

struct FrameData
{
	glm::mat4 MVP;
	GLuint64 diffuseHandle;
};

struct Primitive
{
	GLuint VAO = 0, VBO = 0, EBO = 0;
	GLsizei indexCount = 0;
	GLuint textureId = 0;
	GLuint64 textureHandle = 0;
};

struct Model
{
	std::vector<Primitive> primitives;
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

static bool LoadImageFromMemory(const unsigned char* data, size_t size, GLuint& outTexId, GLuint64& outHandle)
{
	int width, height, channels;
	//stbi_set_flip_vertically_on_load(true);
	unsigned char* pixels = stbi_load_from_memory(data, (int)size, &width, &height, &channels, 4);
	if (!pixels)
	{
		std::cerr << "STBI load from memory failed: " << stbi_failure_reason() << std::endl;
		return false;
	}

	GLuint texture;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);
	GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(width, height)))) + 1;
	glTextureStorage2D(texture, levels, GL_RGBA8, width, height);
	glTextureSubImage2D(texture, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glGenerateTextureMipmap(texture);
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

static const unsigned char* GetAccessorData(const tg3_model& model, int accessorIndex, size_t& outStride, size_t& outCount)
{
	const tg3_accessor& accessor = model.accessors[accessorIndex];
	outCount = (size_t)accessor.count;

	if (accessor.buffer_view < 0)
	{
		outStride = 0;
		return nullptr;
	}

	const tg3_buffer_view& view = model.buffer_views[accessor.buffer_view];
	const tg3_buffer& buffer = model.buffers[view.buffer];

	int32_t stride = tg3_accessor_byte_stride(&accessor, &view);
	outStride = stride > 0 ? (size_t)stride : 0;

	return buffer.data.data + view.byte_offset + accessor.byte_offset;
}

static Model LoadModel(const std::string& filePath)
{
	Model result;

	if (!std::filesystem::exists(filePath))
	{
		std::cerr << "File does not exist: " << filePath << std::endl;
		return result;
	}

	std::cout << "Loading GLTF: " << filePath << std::endl;
	tg3_model model;
	tg3_parse_options opts;
	tg3_error_stack errors;

	tg3_parse_options_init(&opts);
	opts.images_as_is = 1;

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
		return result;
	}
	tg3_error_stack_free(&errors);

	std::filesystem::path baseDir = std::filesystem::path(filePath).parent_path();

	// Resolve each glTF image to a GL texture + bindless handle up front,
	// indexed the same way model.images is indexed.
	std::vector<GLuint> imageTexIds(model.images_count, 0);
	std::vector<GLuint64> imageHandles(model.images_count, 0);

	for (uint32_t i = 0; i < model.images_count; ++i)
	{
		const tg3_image& image = model.images[i];
		GLuint texId = 0;
		GLuint64 handle = 0;
		bool ok = false;

		if (image.uri.data != nullptr && image.uri.len > 0)
		{
			// External image file, referenced relative to the .gltf file.
			std::cerr << "External image file not currently supported.\n";
			return {};
		}
		else if (image.buffer_view >= 0)
		{
			// Embedded image
			const tg3_buffer_view& view = model.buffer_views[image.buffer_view];
			const tg3_buffer& buffer = model.buffers[view.buffer];
			const unsigned char* bytes = buffer.data.data + view.byte_offset;
			ok = LoadImageFromMemory(bytes, (size_t)view.byte_length, texId, handle);
		}

		if (ok)
		{
			imageTexIds[i] = texId;
			imageHandles[i] = handle;
		}
		else
		{
			std::cerr << "Failed to load image index " << i << " for model " << filePath << std::endl;
		}
	}

	// Walk every mesh primitive, upload its vertex/index data
	for (uint32_t m = 0; m < model.meshes_count; ++m)
	{
		const tg3_mesh& mesh = model.meshes[m];

		for (uint32_t p = 0; p < mesh.primitives_count; ++p)
		{
			const tg3_primitive& prim = mesh.primitives[p];

			int positionAccessor = -1;
			int texCoordAccessor = -1;
			for (uint32_t a = 0; a < prim.attributes_count; ++a)
			{
				const tg3_str_int_pair& attr = prim.attributes[a];
				if (tg3_str_equals_cstr(attr.key, "POSITION")) positionAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "TEXCOORD_0")) texCoordAccessor = attr.value;
			}

			if (positionAccessor < 0)
			{
				std::cerr << "Primitive missing POSITION attribute, skipping" << std::endl;
				continue;
			}

			size_t posStride = 0, posCount = 0;
			const unsigned char* posData = GetAccessorData(model, positionAccessor, posStride, posCount);
			if (!posData)
			{
				std::cerr << "POSITION accessor has no backing buffer view, skipping primitive" << std::endl;
				continue;
			}

			const unsigned char* uvData = nullptr;
			size_t uvStride = 0, uvCount = 0;
			if (texCoordAccessor >= 0)
			{
				uvData = GetAccessorData(model, texCoordAccessor, uvStride, uvCount);
			}

			std::vector<Vertex> vertices(posCount);
			for (size_t i = 0; i < posCount; ++i)
			{
				glm::vec3 pos;
				std::memcpy(&pos, posData + i * posStride, sizeof(glm::vec3));
				vertices[i].position = pos;

				if (uvData)
				{
					glm::vec2 uv;
					std::memcpy(&uv, uvData + i * uvStride, sizeof(glm::vec2));
					vertices[i].texCoord = uv;
				}
				else
				{
					vertices[i].texCoord = glm::vec2(0.0f);
				}
			}

			// Indices - component type can vary (u8/u16/u32), normalize to u32.
			std::vector<uint32_t> indices;
			if (prim.indices >= 0)
			{
				const tg3_accessor& idxAccessor = model.accessors[prim.indices];
				size_t idxStride = 0, idxCount = 0;
				const unsigned char* idxData = GetAccessorData(model, prim.indices, idxStride, idxCount);

				if (idxData)
				{
					indices.resize(idxCount);
					if (idxAccessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						for (size_t i = 0; i < idxCount; ++i) { indices[i] = *(idxData + i * idxStride); }
					}
					else if (idxAccessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_SHORT)
					{
						for (size_t i = 0; i < idxCount; ++i)
						{
							uint16_t v;
							std::memcpy(&v, idxData + i * idxStride, sizeof(uint16_t));
							indices[i] = v;
						}
					}
					else if (idxAccessor.component_type == TG3_COMPONENT_TYPE_UNSIGNED_INT)
					{
						for (size_t i = 0; i < idxCount; ++i) { std::memcpy(&indices[i], idxData + i * idxStride, sizeof(uint32_t)); }
					}
				}
			}

			if (indices.empty())
			{
				// No index buffer (or it failed to resolve): draw as a flat list.
				indices.resize(posCount);
				for (size_t i = 0; i < posCount; ++i) { indices[i] = (uint32_t)i; }
			}

			Primitive gpuPrim;
			gpuPrim.indexCount = (GLsizei)indices.size();

			glCreateVertexArrays(1, &gpuPrim.VAO);
			glCreateBuffers(1, &gpuPrim.VBO);
			glCreateBuffers(1, &gpuPrim.EBO);

			glNamedBufferStorage(gpuPrim.VBO, vertices.size() * sizeof(Vertex), vertices.data(), 0);
			glNamedBufferStorage(gpuPrim.EBO, indices.size() * sizeof(uint32_t), indices.data(), 0);

			glVertexArrayVertexBuffer(gpuPrim.VAO, 0, gpuPrim.VBO, 0, sizeof(Vertex));
			glVertexArrayElementBuffer(gpuPrim.VAO, gpuPrim.EBO);

			glEnableVertexArrayAttrib(gpuPrim.VAO, 0);
			glVertexArrayAttribFormat(gpuPrim.VAO, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
			glVertexArrayAttribBinding(gpuPrim.VAO, 0, 0);

			glEnableVertexArrayAttrib(gpuPrim.VAO, 1);
			glVertexArrayAttribFormat(gpuPrim.VAO, 1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texCoord));
			glVertexArrayAttribBinding(gpuPrim.VAO, 1, 0);

			// Resolve base color texture through material -> texture -> image.
			if (prim.material >= 0)
			{
				const tg3_material& material = model.materials[prim.material];
				int32_t texIndex = material.pbr_metallic_roughness.base_color_texture.index;
				if (texIndex >= 0 && (uint32_t)texIndex < model.textures_count)
				{
					int32_t imageIndex = model.textures[texIndex].source;
					if (imageIndex >= 0 && (size_t)imageIndex < imageHandles.size())
					{
						gpuPrim.textureId = imageTexIds[imageIndex];
						gpuPrim.textureHandle = imageHandles[imageIndex];
					}
				}
			}
			result.primitives.push_back(gpuPrim);
		}
	}
	tg3_model_free(&model);

	return result;
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

	// Load Ibex model
	Model ibex = LoadModel("../../../resources/models/ibex.glb");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float time = (float)glfwGetTime();

		// MVP
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 projection = glm::perspective(glm::radians(60.0f), 800.0f / 600.0f, 0.1f, 100.0f);
		glm::mat4 MVP = projection * view * model;

		// Clear color
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render Ibex
		glUseProgram(shaderProgram);
		for (const Primitive& prim : ibex.primitives)
		{
			FrameData frameData{ .MVP = MVP, .diffuseHandle = prim.textureHandle };
			glNamedBufferSubData(SSBO, 0, sizeof(FrameData), &frameData);

			glBindVertexArray(prim.VAO);
			glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT, nullptr);
		}

		glfwSwapBuffers(window);
	}

	// clean up model buffers
	for (const Primitive& prim : ibex.primitives)
	{
		glDeleteVertexArrays(1, &prim.VAO);
		glDeleteBuffers(1, &prim.VBO);
		glDeleteBuffers(1, &prim.EBO);
		if (prim.textureHandle)
		{
			glMakeTextureHandleNonResidentARB(prim.textureHandle);
		}
		if (prim.textureId)
		{
			glDeleteTextures(1, &prim.textureId);
		}
	}
	glDeleteBuffers(1, &SSBO);
	glDeleteProgram(shaderProgram);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}