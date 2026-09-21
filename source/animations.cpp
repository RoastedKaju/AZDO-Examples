#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <stb_image.h>
#include <tiny_gltf_v3.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <functional>
#include <algorithm>

static int sWidth = 800;
static int sHeight = 600;

const char* vertShaderSource = R"(
#version 460 core
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aTangent;
layout (location = 4) in uvec4 aJoints;
layout (location = 5) in vec4 aWeights;

layout (location = 0) out vec2 vTexCoord;

struct FrameData {
	mat4 model;
	mat4 view;
	mat4 proj;
	vec4 baseColorFactor;
	uvec2 baseColorHandle;
};

layout (std430, binding = 0) buffer SceneData {
	FrameData frameData;
};

layout (std430, binding = 1) readonly buffer Bones {
	mat4 bones[];
};

uniform int uSkinned;

void main() {
	vTexCoord = aTexCoord;

	vec4 pos = vec4(aPos, 1.0);
	if (uSkinned != 0) {
		mat4 skin = aWeights.x * bones[aJoints.x] + aWeights.y * bones[aJoints.y] + aWeights.z * bones[aJoints.z] + aWeights.w * bones[aJoints.w];
		pos = skin * pos;
	}
	gl_Position = frameData.proj * frameData.view * frameData.model * pos;
}
)";

const char* fragShaderSource = R"(
#version 460 core
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;
layout (location = 0) out vec4 FragColor;

struct FrameData {
	mat4 model;
	mat4 view;
	mat4 proj;
	vec4 baseColorFactor;
	uvec2 baseColorHandle;
};

layout (std430, binding = 0) buffer SceneData {
	FrameData frameData;
};

void main() {
	sampler2D tex = sampler2D(frameData.baseColorHandle);
	FragColor = texture(tex, vTexCoord) * frameData.baseColorFactor;
}
)";

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

struct Vertex
{
	glm::vec3 position;
	glm::vec2 texCoord;
	glm::vec3 normal;
	glm::vec4 tangent;
	glm::uvec4 joints;
	glm::vec4 weights;
};

struct FrameData
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec4 baseColorFactor;
	GLuint64 baseColorHandle;
};

// Mesh
struct Primitive
{
	GLuint VAO = 0, VBO = 0, EBO = 0;
	GLsizei indexCount = 0;
	GLuint64 baseColorHandle = 0;
	GLuint64 normalHandle = 0;
	GLuint64 metallicRoughnessHandle = 0;
	GLuint64 occlusionHandle = 0;
	GLuint64 emissiveHandle = 0;
	glm::vec4 baseColorFactor = glm::vec4(1.0f);
};

struct Joint
{
	std::string name;
	int parent = -1;
	glm::mat4 inverseBind{ 1.0f };
	glm::vec3 restT{ 0.0f };
	glm::quat restR{ 1.0f, 0.0f, 0.0f, 0.0f };
	glm::vec3 restS{ 1.0f };
	glm::mat4 parentGlobal{ 1.0f };
};

struct Skeleton
{
	std::vector<Joint> joints;
	std::vector<int> order;
};

struct Model
{
	std::vector<Primitive> primitives;
	std::vector<GLuint> textureIds;
	std::vector<GLuint64> textureHandles;
	Skeleton skeleton;
};

// Animation structs
/**
* A channel represents the change of a single property on a single target object over time
*/
struct Channel
{
	enum Path { Translation, Rotation, Scale } path = Translation;
	enum Interp { Linear, Step, CubicSpline }    interp = Linear;

	int targetNode = -1;			// node index in the file the clip was loaded from
	std::string targetName;			// node name, for binding to a skeleton

	std::vector<float>     times;
	std::vector<glm::vec4> values;			// one per key: Vec-3 in XYZ, Quat in XYZW (glTF order)
	std::vector<glm::vec4> inTangents;		// Cubic-Spline only, same length as values
	std::vector<glm::vec4> outTangents;		// Cubic-Spline only
};

struct AnimationClip
{
	std::string name;
	float duration = 0.0f;
	std::vector<Channel> channels;
};

struct FallbackTextures
{
	GLuint64 baseColorHandle = 0;
	GLuint64 normalHandle = 0;
	GLuint64 metallicRoughnessHandle = 0;
	GLuint64 occlusionHandle = 0;
	GLuint64 emissiveHandle = 0;

	std::vector<GLuint> textures;

	void Initialize()
	{
		const uint8_t white[4] = { 255, 255, 255, 255 }; // Albedo / Occlusion
		const uint8_t flatNormal[4] = { 128, 128, 255, 255 }; // Tangent Normal (+Z)
		const uint8_t metallicRoughness[4] = { 0, 255,   0, 255 }; // Roughness = 1.0, Metallic = 0.0
		const uint8_t black[4] = { 0,   0,   0, 255 }; // Emissive (Off)

		baseColorHandle = Create1x1Handle(white);
		normalHandle = Create1x1Handle(flatNormal);
		metallicRoughnessHandle = Create1x1Handle(metallicRoughness);
		occlusionHandle = Create1x1Handle(white);
		emissiveHandle = Create1x1Handle(black);
	}

	void Cleanup()
	{
		for (GLuint64 handle : {baseColorHandle, normalHandle, metallicRoughnessHandle, occlusionHandle, emissiveHandle})
		{
			if (handle) glMakeTextureHandleNonResidentARB(handle);
		}
		for (GLuint tex : textures)
		{
			if (tex) glDeleteTextures(1, &tex);
		}
	}

private:
	GLuint64 Create1x1Handle(const uint8_t pixel[4])
	{
		GLuint texture = 0;
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);
		glTextureStorage2D(texture, 1, GL_RGBA8, 1, 1);
		glTextureSubImage2D(texture, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);

		glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		GLuint64 handle = glGetTextureHandleARB(texture);
		if (handle != 0)
		{
			glMakeTextureHandleResidentARB(handle);
			textures.push_back(texture);
		}
		return handle;
	}
};

static GLuint64 ResolveTextureHandle(const tg3_model& model, int32_t textureIndex, const std::vector<GLuint64>& imageHandle)
{
	if (textureIndex < 0 || (uint32_t)textureIndex >= model.textures_count)
	{
		return 0;
	}

	int32_t imageIndex = model.textures[textureIndex].source;
	if (imageIndex < 0 || (size_t)imageIndex >= imageHandle.size())
	{
		return 0;
	}

	return imageHandle[imageIndex];
}

static bool LoadImageFromMemory(const unsigned char* data, size_t size, GLuint& outTexId, GLuint64& outHandle)
{
	int width, height, channels;
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

static glm::mat4 NodeLocalMatrix(const tg3_node& n, glm::vec3& t, glm::quat& r, glm::vec3& s)
{
	if (n.has_matrix)
	{
		glm::mat4 m;
		for (int i = 0; i < 16; ++i) m[i / 4][i % 4] = (float)n.matrix[i];  // column-major
		glm::vec3 skew; glm::vec4 persp;
		glm::decompose(m, s, r, t, skew, persp);
		return m;
	}

	t = glm::vec3((float)n.translation[0], (float)n.translation[1], (float)n.translation[2]);
	r = glm::quat((float)n.rotation[3], (float)n.rotation[0], (float)n.rotation[1], (float)n.rotation[2]);
	s = glm::vec3((float)n.scale[0], (float)n.scale[1], (float)n.scale[2]);
	return glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(glm::normalize(r)) * glm::scale(glm::mat4(1.0f), s);
}

static Skeleton BuildSkeleton(const tg3_model& model)
{
	Skeleton skel;
	if (model.skins_count == 0) return skel;

	const tg3_skin& skin = model.skins[0]; // assumes one skin
	const uint32_t jointCount = skin.joints_count;

	if (model.skins_count > 1)
	{
		std::cerr << "Warning: " << model.skins_count << " skins found, only using skin 0\n";
	}

	// node -> parent node, node -> joint index
	std::vector<int> nodeParent(model.nodes_count, -1);
	for (uint32_t n = 0; n < model.nodes_count; ++n)
		for (uint32_t c = 0; c < model.nodes[n].children_count; ++c)
			nodeParent[model.nodes[n].children[c]] = (int)n;

	std::vector<int> nodeToJoint(model.nodes_count, -1);
	for (uint32_t j = 0; j < jointCount; ++j)
		nodeToJoint[skin.joints[j]] = (int)j;

	// Inverse bind matrices
	std::vector<glm::mat4> ibm(jointCount, glm::mat4(1.0f));
	if (skin.inverse_bind_matrices >= 0)
	{
		size_t stride = 0, count = 0;
		const unsigned char* data = GetAccessorData(model, skin.inverse_bind_matrices, stride, count);
		if (data)
		{
			for (size_t i = 0; i < std::min<size_t>(count, jointCount); ++i)
				std::memcpy(&ibm[i], data + i * stride, sizeof(glm::mat4));
		}
	}

	skel.joints.resize(jointCount);
	for (uint32_t j = 0; j < jointCount; ++j)
	{
		const int nodeIdx = skin.joints[j];
		const tg3_node& node = model.nodes[nodeIdx];
		Joint& joint = skel.joints[j];

		joint.name.assign((const char*)node.name.data, node.name.len);
		joint.inverseBind = ibm[j];
		NodeLocalMatrix(node, joint.restT, joint.restR, joint.restS);

		const int parentNode = nodeParent[nodeIdx];
		joint.parent = (parentNode >= 0) ? nodeToJoint[parentNode] : -1;

		// If the parent isn't a joint (e.g. "Armature" with a 0.01 scale), bake its
		// accumulated transform so root joints still end up in the right space.
		if (joint.parent < 0)
		{
			glm::mat4 acc(1.0f);
			glm::vec3 t, s; glm::quat r;
			for (int n = parentNode; n >= 0; n = nodeParent[n])
				acc = NodeLocalMatrix(model.nodes[n], t, r, s) * acc;
			joint.parentGlobal = acc;
		}
	}

	// Parents before children evaluation order
	std::vector<char> visited(jointCount, 0);
	skel.order.reserve(jointCount);
	std::function<void(int)> visit = [&](int j)
		{
			if (visited[j]) return;
			visited[j] = 1;
			if (skel.joints[j].parent >= 0) visit(skel.joints[j].parent);
			skel.order.push_back(j);
		};
	for (int j = 0; j < (int)jointCount; ++j) visit(j);

	std::cout << "Skeleton: " << jointCount << " joints\n";
	for (uint32_t j = 0; j < jointCount; ++j)
		std::cout << "  joint " << j << ": " << skel.joints[j].name << " parent=" << skel.joints[j].parent << '\n';

	return skel;
}

static Model LoadModel(const std::string& filePath, const FallbackTextures& fallbacks)
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
			tg3_model_free(&model);
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
			int normalAccessor = -1;
			int tangentAccessor = -1;
			int jointsAccessor = -1;
			int weightsAccessor = -1;
			for (uint32_t a = 0; a < prim.attributes_count; ++a)
			{
				const tg3_str_int_pair& attr = prim.attributes[a];
				if (tg3_str_equals_cstr(attr.key, "POSITION")) positionAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "TEXCOORD_0")) texCoordAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "NORMAL")) normalAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "TANGENT")) tangentAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "JOINTS_0")) jointsAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "WEIGHTS_0")) weightsAccessor = attr.value;
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

			// normal
			const unsigned char* normData = nullptr;
			size_t normStride = 0, normCount = 0;
			if (normalAccessor >= 0)
			{
				normData = GetAccessorData(model, normalAccessor, normStride, normCount);
			}

			// tangent
			const unsigned char* tangentData = nullptr;
			size_t tangentStride = 0, tangentCount = 0;
			if (tangentAccessor >= 0)
			{
				tangentData = GetAccessorData(model, tangentAccessor, tangentStride, tangentCount);
			}

			// joints
			const unsigned char* jointsData = nullptr;
			size_t jointsStride = 0, jointsCount = 0;
			if (jointsAccessor >= 0)
			{
				jointsData = GetAccessorData(model, jointsAccessor, jointsStride, jointsCount);
			}

			// weights
			const unsigned char* weightsData = nullptr;
			size_t weightsStride = 0, weightsCount = 0;
			if (weightsAccessor >= 0)
			{
				weightsData = GetAccessorData(model, weightsAccessor, weightsStride, weightsCount);
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

				if (normData)
				{
					glm::vec3 norm;
					std::memcpy(&norm, normData + i * normStride, sizeof(glm::vec3));
					vertices[i].normal = norm;
				}
				else
				{
					vertices[i].normal = glm::vec3(0.0f);
				}

				if (tangentData)
				{
					glm::vec4 tangent;
					std::memcpy(&tangent, tangentData + i * tangentStride, sizeof(glm::vec4));
					vertices[i].tangent = tangent;
				}
				else
				{
					vertices[i].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
				}

				if (jointsData)
				{
					const int compType = model.accessors[jointsAccessor].component_type;
					const unsigned char* p = jointsData + i * jointsStride;
					if (compType == TG3_COMPONENT_TYPE_UNSIGNED_BYTE)
					{
						vertices[i].joints = glm::uvec4(p[0], p[1], p[2], p[3]);
					}
					else // UNSIGNED_SHORT
					{
						uint16_t j[4];
						std::memcpy(j, p, sizeof(j));
						vertices[i].joints = glm::uvec4(j[0], j[1], j[2], j[3]);
					}
				}

				if (weightsData)
				{
					const tg3_accessor& wa = model.accessors[weightsAccessor];
					const unsigned char* p = weightsData + i * weightsStride;
					if (wa.component_type == TG3_COMPONENT_TYPE_FLOAT)
					{
						std::memcpy(&vertices[i].weights, p, sizeof(glm::vec4));
					}
					else if (wa.component_type == TG3_COMPONENT_TYPE_UNSIGNED_BYTE) // normalized
					{
						vertices[i].weights = glm::vec4(p[0], p[1], p[2], p[3]) / 255.0f;
					}
					else // normalized u16
					{
						uint16_t w[4];
						std::memcpy(w, p, sizeof(w));
						vertices[i].weights = glm::vec4(w[0], w[1], w[2], w[3]) / 65535.0f;
					}
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

			glEnableVertexArrayAttrib(gpuPrim.VAO, 2);
			glVertexArrayAttribFormat(gpuPrim.VAO, 2, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
			glVertexArrayAttribBinding(gpuPrim.VAO, 2, 0);

			glEnableVertexArrayAttrib(gpuPrim.VAO, 3);
			glVertexArrayAttribFormat(gpuPrim.VAO, 3, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, tangent));
			glVertexArrayAttribBinding(gpuPrim.VAO, 3, 0);

			glEnableVertexArrayAttrib(gpuPrim.VAO, 4);
			glVertexArrayAttribIFormat(gpuPrim.VAO, 4, 4, GL_UNSIGNED_INT, offsetof(Vertex, joints));
			glVertexArrayAttribBinding(gpuPrim.VAO, 4, 0);

			glEnableVertexArrayAttrib(gpuPrim.VAO, 5);
			glVertexArrayAttribFormat(gpuPrim.VAO, 5, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, weights));
			glVertexArrayAttribBinding(gpuPrim.VAO, 5, 0);

			// Resolve base color texture through material -> texture -> image.
			if (prim.material >= 0 && (uint32_t)prim.material < model.materials_count)
			{
				const tg3_material& material = model.materials[prim.material];

				gpuPrim.baseColorHandle = ResolveTextureHandle(model, material.pbr_metallic_roughness.base_color_texture.index, imageHandles);
				gpuPrim.metallicRoughnessHandle = ResolveTextureHandle(model, material.pbr_metallic_roughness.metallic_roughness_texture.index, imageHandles);
				gpuPrim.normalHandle = ResolveTextureHandle(model, material.normal_texture.index, imageHandles);
				gpuPrim.occlusionHandle = ResolveTextureHandle(model, material.occlusion_texture.index, imageHandles);
				gpuPrim.emissiveHandle = ResolveTextureHandle(model, material.emissive_texture.index, imageHandles);

				// Base color factor
				const auto& bc = material.pbr_metallic_roughness.base_color_factor;
				gpuPrim.baseColorFactor = glm::vec4((float)bc[0], (float)bc[1], (float)bc[2], (float)bc[3]);
			}

			// Apply Fallbacks if handles failed to load or were missing
			if (gpuPrim.baseColorHandle == 0)         gpuPrim.baseColorHandle = fallbacks.baseColorHandle;
			if (gpuPrim.normalHandle == 0)            gpuPrim.normalHandle = fallbacks.normalHandle;
			if (gpuPrim.metallicRoughnessHandle == 0) gpuPrim.metallicRoughnessHandle = fallbacks.metallicRoughnessHandle;
			if (gpuPrim.occlusionHandle == 0)         gpuPrim.occlusionHandle = fallbacks.occlusionHandle;
			if (gpuPrim.emissiveHandle == 0)          gpuPrim.emissiveHandle = fallbacks.emissiveHandle;

			result.primitives.push_back(gpuPrim);
		}
	}

	result.skeleton = BuildSkeleton(model);
	result.textureIds = std::move(imageTexIds);
	result.textureHandles = std::move(imageHandles);

	tg3_model_free(&model);

	return result;
}

// Reads any accessor into a flat float array, handling normalized integer types.
static bool ReadAccessorFloats(const tg3_model& model, int idx, std::vector<float>& out, int& comps)
{
	if (idx < 0 || idx >= (int)model.accessors_count) return false;
	const tg3_accessor& acc = model.accessors[idx];
	comps = tg3_num_components(acc.type);
	const int csize = tg3_component_size(acc.component_type);

	size_t stride = 0, count = 0;
	const unsigned char* data = GetAccessorData(model, idx, stride, count);
	if (!data || comps <= 0 || csize <= 0) return false;

	out.resize(count * comps);
	for (size_t i = 0; i < count; ++i)
	{
		for (int c = 0; c < comps; ++c)
		{
			const unsigned char* p = data + i * stride + c * csize;
			float v = 0.0f;
			switch (acc.component_type)
			{
			case TG3_COMPONENT_TYPE_FLOAT: std::memcpy(&v, p, 4); break;
			case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: v = *p / (acc.normalized ? 255.0f : 1.0f); break;
			case TG3_COMPONENT_TYPE_BYTE: { int8_t x; std::memcpy(&x, p, 1); v = acc.normalized ? std::max(x / 127.0f, -1.0f) : (float)x; } break;
			case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: { uint16_t x; std::memcpy(&x, p, 2); v = acc.normalized ? x / 65535.0f : (float)x; } break;
			case TG3_COMPONENT_TYPE_SHORT: { int16_t x; std::memcpy(&x, p, 2); v = acc.normalized ? std::max(x / 32767.0f, -1.0f) : (float)x; } break;
			default: return false;
			}
			out[i * comps + c] = v;
		}
	}
	return true;
}

static std::string ToString(const tg3_str& s)
{
	return (s.data && s.len) ? std::string(s.data, s.len) : std::string();
}

static std::vector<AnimationClip> LoadAnimations(const std::string& filePath)
{
	std::vector<AnimationClip> clips;

	if (!std::filesystem::exists(filePath))
	{
		std::cerr << "Animation file does not exist: " << filePath << std::endl;
		return clips;
	}

	tg3_model model;
	tg3_parse_options opts;
	tg3_error_stack errors;
	tg3_parse_options_init(&opts);
	opts.images_as_is = 1;
	tg3_error_stack_init(&errors);

	if (tg3_parse_file(&model, &errors, filePath.c_str(), (uint32_t)filePath.size(), &opts) != TG3_OK)
	{
		for (uint32_t i = 0; i < errors.count; ++i) std::cerr << errors.entries[i].message << std::endl;
		tg3_error_stack_free(&errors);
		return clips;
	}
	tg3_error_stack_free(&errors);

	for (uint32_t a = 0; a < model.animations_count; ++a)
	{
		const tg3_animation& anim = model.animations[a];

		AnimationClip clip;
		clip.name = ToString(anim.name);
		if (clip.name.empty()) clip.name = "animation_" + std::to_string(a);

		int skipped = 0;
		for (uint32_t c = 0; c < anim.channels_count; ++c)
		{
			const tg3_animation_channel& ch = anim.channels[c];
			if (ch.target.node < 0 || (uint32_t)ch.target.node >= model.nodes_count || ch.sampler < 0 || (uint32_t)ch.sampler >= anim.samplers_count)
			{
				++skipped;
				continue;
			}

			Channel out;
			int expectedComps;
			if (tg3_str_equals_cstr(ch.target.path, "translation")) { out.path = Channel::Translation; expectedComps = 3; }
			else if (tg3_str_equals_cstr(ch.target.path, "rotation")) { out.path = Channel::Rotation; expectedComps = 4; }
			else if (tg3_str_equals_cstr(ch.target.path, "scale")) { out.path = Channel::Scale; expectedComps = 3; }
			else { ++skipped; continue; }   // "weights" (morph targets) not supported yet

			out.targetNode = ch.target.node;
			out.targetName = ToString(model.nodes[ch.target.node].name);

			const tg3_animation_sampler& smp = anim.samplers[ch.sampler];
			if (tg3_str_equals_cstr(smp.interpolation, "STEP")) { out.interp = Channel::Step; }
			else if (tg3_str_equals_cstr(smp.interpolation, "CUBICSPLINE")) { out.interp = Channel::CubicSpline; }

			std::vector<float> t, v;
			int tc = 0, vc = 0;
			if (!ReadAccessorFloats(model, smp.input, t, tc) || !ReadAccessorFloats(model, smp.output, v, vc) || t.empty() || vc != expectedComps)
			{
				++skipped;
				continue;
			}

			// Cubic Spline output is (in-tangent, value, out-tangent) per key; the others are one value per key.
			const bool cubic = (out.interp == Channel::CubicSpline);
			const size_t perKey = cubic ? 3 : 1;
			if (v.size() < t.size() * perKey * vc) { ++skipped; continue; }

			auto read = [&](size_t idx) {
				const float* p = &v[idx * vc];
				return glm::vec4(p[0], p[1], p[2], vc == 4 ? p[3] : 0.0f);
				};

			out.times = std::move(t);
			out.values.resize(out.times.size());
			if (cubic)
			{
				out.inTangents.resize(out.times.size());
				out.outTangents.resize(out.times.size());
			}
			for (size_t k = 0; k < out.times.size(); ++k)
			{
				if (cubic)
				{
					out.inTangents[k] = read(k * 3 + 0);
					out.values[k] = read(k * 3 + 1);
					out.outTangents[k] = read(k * 3 + 2);
				}
				else
				{
					out.values[k] = read(k);
				}
			}

			clip.duration = std::max(clip.duration, out.times.back());
			clip.channels.push_back(std::move(out));
		}

		std::cout << "Animation '" << clip.name << "': " << clip.channels.size() << " channels, " << clip.duration << "s";
		if (skipped) std::cout << " (" << skipped << " channels skipped)";
		std::cout << '\n';

		clips.push_back(std::move(clip));
	}

	tg3_model_free(&model);
	return clips;
}

// Animation Playback
// Resolves each channel's target name to a joint index. Exact-name matching
static std::vector<int> BindClip(const AnimationClip& clip, const Skeleton& skel)
{
	std::unordered_map<std::string, int> byName;
	for (int i = 0; i < (int)skel.joints.size(); ++i) byName.emplace(skel.joints[i].name, i);

	std::vector<int> binding(clip.channels.size(), -1);
	int unmatched = 0;
	for (size_t c = 0; c < clip.channels.size(); ++c)
	{
		auto it = byName.find(clip.channels[c].targetName);
		if (it != byName.end()) binding[c] = it->second;
		else ++unmatched;
	}
	if (unmatched)
	{
		std::cerr << "BindClip '" << clip.name << "': " << unmatched << " of " << clip.channels.size() << " channels have no matching joint\n";
	}
	return binding;
}

static glm::vec4 SampleChannel(const Channel& ch, float t)
{
	const auto& ts = ch.times;
	if (ts.size() == 1 || t <= ts.front()) return ch.values.front();
	if (t >= ts.back())                    return ch.values.back();

	const size_t i1 = std::upper_bound(ts.begin(), ts.end(), t) - ts.begin();
	const size_t i0 = i1 - 1;
	const float  dt = ts[i1] - ts[i0];
	const float  s = (t - ts[i0]) / dt;
	const glm::vec4 v0 = ch.values[i0];
	const glm::vec4 v1 = ch.values[i1];
	const bool isRot = (ch.path == Channel::Rotation);

	switch (ch.interp)
	{
	case Channel::Step:
		return v0;

	case Channel::CubicSpline:
	{
		const glm::vec4 m0 = ch.outTangents[i0] * dt;
		const glm::vec4 m1 = ch.inTangents[i1] * dt;
		const float s2 = s * s, s3 = s2 * s;
		glm::vec4 r = (2 * s3 - 3 * s2 + 1) * v0 + (s3 - 2 * s2 + s) * m0
			+ (-2 * s3 + 3 * s2) * v1 + (s3 - s2) * m1;
		return isRot ? glm::normalize(r) : r;
	}

	default: // Linear
		if (isRot)
		{
			glm::quat q = glm::slerp(glm::quat(v0.w, v0.x, v0.y, v0.z), glm::quat(v1.w, v1.x, v1.y, v1.z), s);
			return glm::vec4(q.x, q.y, q.z, q.w);
		}
		return glm::mix(v0, v1, s);
	}
}

// Fills out skin with one skinning matrix per joint (indexed like JOINTS_0).
static void EvaluatePose(const Skeleton& skel, const AnimationClip& clip, const std::vector<int>& binding, float time, std::vector<glm::mat4>& outSkin)
{
	const size_t n = skel.joints.size();
	std::vector<glm::vec3> T(n), S(n);
	std::vector<glm::quat> R(n);
	for (size_t i = 0; i < n; ++i) { T[i] = skel.joints[i].restT; R[i] = skel.joints[i].restR; S[i] = skel.joints[i].restS; }

	for (size_t c = 0; c < clip.channels.size() && c < binding.size(); ++c)
	{
		const int j = binding[c];
		if (j < 0) continue;
		const Channel& ch = clip.channels[c];
		const glm::vec4 v = SampleChannel(ch, time);
		switch (ch.path)
		{
		case Channel::Translation: T[j] = glm::vec3(v); break;
		case Channel::Rotation:    R[j] = glm::quat(v.w, v.x, v.y, v.z); break;
		case Channel::Scale:       S[j] = glm::vec3(v); break;
		}
	}

	std::vector<glm::mat4> global(n);
	outSkin.resize(n);
	for (int j : skel.order)
	{
		const Joint& joint = skel.joints[j];
		const glm::mat4 local = glm::translate(glm::mat4(1.0f), T[j]) * glm::mat4_cast(glm::normalize(R[j])) * glm::scale(glm::mat4(1.0f), S[j]);
		const glm::mat4& parent = (joint.parent >= 0) ? global[joint.parent] : joint.parentGlobal;
		global[j] = parent * local;
		outSkin[j] = global[j] * joint.inverseBind;
	}
}

static void PrintBindPoseError(const Skeleton& skel)
{
	std::vector<glm::mat4> skin;
	EvaluatePose(skel, AnimationClip{}, {}, 0.0f, skin);

	float maxErr = 0.0f;
	int worst = -1;
	for (size_t j = 0; j < skin.size(); ++j)
	{
		for (int c = 0; c < 4; ++c)
		{
			for (int r = 0; r < 4; ++r)
			{
				const float err = std::abs(skin[j][c][r] - (c == r ? 1.0f : 0.0f));
				if (err > maxErr) { maxErr = err; worst = (int)j; }
			}
		}
	}
	std::cout << "Bind pose max error: " << maxErr;
	if (worst >= 0) std::cout << " (joint " << worst << " '" << skel.joints[worst].name << "')";
	std::cout << '\n';
}

struct AnimationPlayer
{
	const AnimationClip* clip = nullptr;
	std::vector<int> binding;
	float time = 0.0f;
	float speed = 1.0f;
	bool  loop = true;

	std::vector<glm::mat4> skin;
	GLuint boneSSBO = 0;

	void Init(const Skeleton& skel)
	{
		skin.assign(skel.joints.size(), glm::mat4(1.0f));
		// Always allocate at least one mat4 so the buffer is valid for static models.
		const size_t bytes = std::max<size_t>(skin.size(), 1) * sizeof(glm::mat4);
		glCreateBuffers(1, &boneSSBO);
		glNamedBufferStorage(boneSSBO, bytes, nullptr, GL_DYNAMIC_STORAGE_BIT);
	}

	void Play(const AnimationClip& c, const Skeleton& skel)
	{
		clip = &c;
		binding = BindClip(c, skel);
		time = 0.0f;
	}

	void Update(float dt, const Skeleton& skel)
	{
		if (skin.empty()) return;

		static const AnimationClip emptyClip;
		if (clip && clip->duration > 0.0f)
		{
			time += dt * speed;
			time = loop ? std::fmod(time, clip->duration) : std::min(time, clip->duration);
		}
		EvaluatePose(skel, clip ? *clip : emptyClip, binding, time, skin);
		glNamedBufferSubData(boneSSBO, 0, skin.size() * sizeof(glm::mat4), skin.data());
	}

	void Destroy()
	{
		if (boneSSBO) { glDeleteBuffers(1, &boneSSBO); }
		boneSSBO = 0;
	}
};

int main()
{
	if (!glfwInit()) return EXIT_FAILURE;

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);

	GLFWwindow* window = glfwCreateWindow(sWidth, sHeight, "Model", nullptr, nullptr);
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

	GLuint shaderProgram = CreateShaderProgram(vertShaderSource, fragShaderSource);

	FallbackTextures defaultTextures{};
	defaultTextures.Initialize();

	// SSBO holding per-frame data
	GLuint SSBO;
	glCreateBuffers(1, &SSBO);
	glNamedBufferStorage(SSBO, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SSBO);

	Model skinnedMesh = LoadModel("../../../resources/models/adventurer.glb", defaultTextures);
	//auto anims = LoadAnimations("../../../resources/animations/taunt.glb");
	auto anims = LoadAnimations("../../../resources/animations/adventurer.glb");


	// Skinning setup. Static models leave uSkinned at 0 and ignore the bone buffer.
	glProgramUniform1i(shaderProgram, glGetUniformLocation(shaderProgram, "uSkinned"), skinnedMesh.skeleton.joints.empty() ? 0 : 1);
	PrintBindPoseError(skinnedMesh.skeleton);

	AnimationPlayer player;
	player.Init(skinnedMesh.skeleton);
	if (!anims.empty())
	{
		player.Play(anims[0], skinnedMesh.skeleton);
	}
	else
	{
		std::cerr << "No animations loaded, showing bind pose\n";
	}

	float lastTime = (float)glfwGetTime();

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (sWidth == 0 || sHeight == 0)
		{
			glfwWaitEvents();
			continue;
		}

		float time = (float)glfwGetTime();
		float deltaTime = time - lastTime;
		lastTime = time;

		player.Update(deltaTime, skinnedMesh.skeleton);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, player.boneSSBO);

		// MVP
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		model = glm::scale(model, glm::vec3(1.0f, 1.0f, 1.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.5f, 2.5f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);

		// Clear color
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render
		glUseProgram(shaderProgram);
		for (const Primitive& prim : skinnedMesh.primitives)
		{
			FrameData frameData
			{
				.model = model,
				.view = view,
				.proj = projection,
				.baseColorFactor = prim.baseColorFactor,
				.baseColorHandle = prim.baseColorHandle
			};
			glNamedBufferSubData(SSBO, 0, sizeof(FrameData), &frameData);

			glBindVertexArray(prim.VAO);
			glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT, nullptr);
		}

		glfwSwapBuffers(window);
	}

	// clean up model buffers
	for (const Primitive& prim : skinnedMesh.primitives)
	{
		glDeleteVertexArrays(1, &prim.VAO);
		glDeleteBuffers(1, &prim.VBO);
		glDeleteBuffers(1, &prim.EBO);
	}
	// clean up every texture
	for (GLuint64 handle : skinnedMesh.textureHandles)
	{
		if (handle) glMakeTextureHandleNonResidentARB(handle);
	}
	for (GLuint texId : skinnedMesh.textureIds)
	{
		if (texId) glDeleteTextures(1, &texId);
	}

	defaultTextures.Cleanup();

	player.Destroy();
	glDeleteBuffers(1, &SSBO);
	glDeleteProgram(shaderProgram);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}