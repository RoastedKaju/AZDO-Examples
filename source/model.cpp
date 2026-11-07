#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>
#include <tiny_gltf_v3.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <array>

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

struct Vertex
{
	glm::vec3 position;
	glm::vec2 texCoord;
	glm::vec3 normal;
	glm::vec4 tangent;
};

struct FrameData
{
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 proj;
};

struct MaterialData
{
	GLuint64 baseColorHandle;
	GLuint64 normalHandle;
	GLuint64 metallicRoughnessHandle;
	GLuint64 occlusionHandle;
	GLuint64 emissiveHandle;
	float roughnessFactor = 1.0f;
	float metallicFactor = 1.0f;
};

struct Primitive
{
	GLuint VAO = 0, VBO = 0, EBO = 0;
	GLsizei indexCount = 0;
	GLuint64 baseColorHandle = 0;
	GLuint64 normalHandle = 0;
	GLuint64 metallicRoughnessHandle = 0;
	GLuint64 occlusionHandle = 0;
	GLuint64 emissiveHandle = 0;
};

struct Model
{
	std::vector<Primitive> primitives;
	std::vector<GLuint> textureIds;
	std::vector<GLuint64> textureHandles;
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

const char* vertexShaderSource = R"(
#version 460 core
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec3 aNormal;
layout (location = 3) in vec4 aTangent;

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) out vec3 vFragPos;
layout (location = 2) out vec3 vNormal;
layout (location = 3) out vec3 vTangent;
layout (location = 4) out vec3 vBitangent;

layout (std140, binding = 0) readonly
uniform FrameData {
	mat4 model;
	mat4 view;
	mat4 proj;
} frameData;

void main() {
	vTexCoord = aTexCoord;
	
	vec4 worldPos = frameData.model * vec4(aPos, 1.0);
	vFragPos = worldPos.xyz;
	
	mat3 normalMatrix = transpose(inverse(mat3(frameData.model)));

	vec3 N = normalize(normalMatrix * aNormal);
	vec3 T = normalize(normalMatrix * aTangent.xyz);
	T = normalize(T - dot(T, N) * N);
	vec3 B = cross(N, T) * aTangent.w;
	
	vNormal = N;
	vTangent = T;
	vBitangent = B;

	gl_Position = frameData.proj * frameData.view * worldPos;
}
)";

const char* fragmentShaderSource = R"(
#version 460 core
#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) in vec3 vFragPos;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in vec3 vTangent;
layout (location = 4) in vec3 vBitangent;

layout (location = 0) out vec4 FragColor;

struct MaterialData {
	uvec2 baseColorHandle;
	uvec2 normalHandle;
	uvec2 metallicRoughnessHandle;
	uvec2 occlusionHandle;
	uvec2 emissiveHandle;
	float roughnessFactor;
	float metallicFactor;
};

layout (location = 1) uniform uint uMaterialIndex;

layout (std430, binding = 0) readonly
buffer MaterialBuffer {
	MaterialData materials[];
};

const float PI = 3.14159265359;

// Cook-Torrance PBR Functions
float DistributionGGX(vec3 N, vec3 H, float roughness) {
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float num = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
	// Sample albedo & convert to linear space
	sampler2D baseTex = sampler2D(materials[uMaterialIndex].baseColorHandle);
	vec4 albedoSample = texture(baseTex, vTexCoord);
	vec3 albedo = pow(albedoSample.rgb, vec3(2.2));

	// Sample packed metallic-roughness map
	sampler2D mrTex = sampler2D(materials[uMaterialIndex].metallicRoughnessHandle);
	vec4 mrSample = texture(mrTex, vTexCoord);

	float roughness = clamp(mrSample.g * materials[uMaterialIndex].roughnessFactor, 0.05, 1.0);
	float metallic  = clamp(mrSample.b * materials[uMaterialIndex].metallicFactor, 0.0, 1.0);

	// Normal mapping
	mat3 TBN = mat3(normalize(vTangent), normalize(vBitangent), normalize(vNormal));
	sampler2D normalTex = sampler2D(materials[uMaterialIndex].normalHandle);
	vec3 tangentNormal = texture(normalTex, vTexCoord).rgb * 2.0 - 1.0;

	float normalStrength = 1.5;
	tangentNormal.xy *= normalStrength;
	tangentNormal = normalize(tangentNormal);
	
	vec3 N = normalize(TBN * tangentNormal);

	// View & Light setup
	vec3 viewPos = vec3(0.0, 1.0, 3.0); 
	vec3 V = normalize(viewPos - vFragPos);
	vec3 L = normalize(vec3(0.5, 1.0, 0.8));
	vec3 H = normalize(V + L);
	vec3 lightColor = vec3(2.5); // Directional Light Intensity

	// Fresnel Reflectivity at Normal Incidence
	vec3 F0 = vec3(0.04); 
	F0 = mix(F0, albedo, metallic);

	// Cook-Torrance BRDF Computation
	float NDF = DistributionGGX(N, H, roughness);
	float G   = GeometrySmith(N, V, L, roughness);
	vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);

	vec3 numerator    = NDF * G * F; 
	float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
	vec3 specular     = numerator / denominator;
	
	vec3 kS = F;
	vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);	

	float NdotL = max(dot(N, L), 0.0);
	vec3 Lo = (kD * albedo / PI + specular) * lightColor * NdotL;

	// Ambient Lighting
	vec3 ambient = vec3(0.03) * albedo;
	vec3 color = ambient + Lo;

	// Reinhard tone mapping & Gamma correction
	// This later should be moved to post processing pass
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0 / 2.6));

	FragColor = vec4(color, albedoSample.a);
}
)";

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
		exit(EXIT_FAILURE);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);
	return program;
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
			for (uint32_t a = 0; a < prim.attributes_count; ++a)
			{
				const tg3_str_int_pair& attr = prim.attributes[a];
				if (tg3_str_equals_cstr(attr.key, "POSITION")) positionAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "TEXCOORD_0")) texCoordAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "NORMAL")) normalAccessor = attr.value;
				else if (tg3_str_equals_cstr(attr.key, "TANGENT")) tangentAccessor = attr.value;
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

			// Resolve base color texture through material -> texture -> image.
			if (prim.material >= 0 && (uint32_t)prim.material < model.materials_count)
			{
				const tg3_material& material = model.materials[prim.material];

				gpuPrim.baseColorHandle = ResolveTextureHandle(model, material.pbr_metallic_roughness.base_color_texture.index, imageHandles);
				gpuPrim.metallicRoughnessHandle = ResolveTextureHandle(model, material.pbr_metallic_roughness.metallic_roughness_texture.index, imageHandles);
				gpuPrim.normalHandle = ResolveTextureHandle(model, material.normal_texture.index, imageHandles);
				gpuPrim.occlusionHandle = ResolveTextureHandle(model, material.occlusion_texture.index, imageHandles);
				gpuPrim.emissiveHandle = ResolveTextureHandle(model, material.emissive_texture.index, imageHandles);
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
	tg3_model_free(&model);

	result.textureIds = std::move(imageTexIds);
	result.textureHandles = std::move(imageHandles);

	return result;
}

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

	GLuint shaderProgram = CreateShaderProgram();

	FallbackTextures defaultTextures{};
	defaultTextures.Initialize();

	// UBO holding per-frame data
	GLuint perFrameUBO;
	glCreateBuffers(1, &perFrameUBO);
	glNamedBufferStorage(perFrameUBO, sizeof(FrameData), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_UNIFORM_BUFFER, 0, perFrameUBO);

	// Load Ibex model
	Model ibex = LoadModel("../../../resources/models/heather.glb", defaultTextures);

	// Allocate space for SSBO for every primitive
	GLuint materialSSBO;
	glCreateBuffers(1, &materialSSBO);
	glNamedBufferStorage(materialSSBO, sizeof(MaterialData) * ibex.primitives.size(), nullptr, GL_DYNAMIC_STORAGE_BIT);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, materialSSBO);

	// Send material data and texture handles once, use offset for different primitives
	for (size_t i = 0; i < ibex.primitives.size(); ++i)
	{
		const Primitive& prim = ibex.primitives[i];

		MaterialData materialData
		{
			.baseColorHandle = prim.baseColorHandle,
			.normalHandle = prim.normalHandle,
			.metallicRoughnessHandle = prim.metallicRoughnessHandle,
			.occlusionHandle = prim.occlusionHandle,
			.emissiveHandle = prim.emissiveHandle,
			.roughnessFactor = 1.0f,
			.metallicFactor = 1.0f
		};
		// Send to GPU with the correct offset
		glNamedBufferSubData(materialSSBO, i * sizeof(MaterialData), sizeof(MaterialData), &materialData);
	}
	// Get material index uniform location
	GLint materialIndexLoc = glGetUniformLocation(shaderProgram, "uMaterialIndex");

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		if (sWidth == 0 || sHeight == 0)
		{
			glfwWaitEvents();
			continue;
		}

		float time = (float)glfwGetTime();

		// MVP
		glm::mat4 model = glm::rotate(glm::mat4(1.0f), time * glm::radians(50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.0f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		glm::mat4 projection = glm::perspective(glm::radians(60.0f), (float)sWidth / (float)sHeight, 0.1f, 100.0f);

		// Clear color
		glClearColor(0.1f, 0.12f, 0.1f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		FrameData frameData
		{
			.model = model,
			.view = view,
			.proj = projection
		};
		glNamedBufferSubData(perFrameUBO, 0, sizeof(FrameData), &frameData);

		// Render Ibex
		glUseProgram(shaderProgram);
		for (size_t i = 0; i < ibex.primitives.size(); ++i)
		{
			// Send index for material data
			const Primitive& prim = ibex.primitives[i];
			glProgramUniform1ui(shaderProgram, materialIndexLoc, (GLuint)i);
			// Draw
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
	}
	// clean up every texture
	for (GLuint64 handle : ibex.textureHandles)
	{
		if (handle) glMakeTextureHandleNonResidentARB(handle);
	}
	for (GLuint texId : ibex.textureIds)
	{
		if (texId) glDeleteTextures(1, &texId);
	}

	defaultTextures.Cleanup();

	glDeleteBuffers(1, &perFrameUBO);
	glDeleteBuffers(1, &materialSSBO);
	glDeleteProgram(shaderProgram);
	glfwDestroyWindow(window);
	glfwTerminate();
	return EXIT_SUCCESS;
}