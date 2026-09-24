#pragma once

#include <glad/gl.h>
#include <tiny_gltf_v3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <common.hpp>
#include <file_manager.hpp>

namespace GlTFImporter
{
	static inline std::string ToString(const tg3_str& s)
	{
		return (s.data && s.len) ? std::string(s.data, s.len) : std::string();
	}

	static inline std::string DecodeURI(const std::string& in)
	{
		auto hex = [](char c) -> int
			{
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return 10 + c - 'a';
				if (c >= 'A' && c <= 'F') return 10 + c - 'A';
				return -1;
			};

		std::string out;
		out.reserve(in.size());
		for (size_t i = 0; i < in.size(); ++i)
		{
			if (in[i] == '%' && i + 2 < in.size() && hex(in[i + 1]) >= 0 && hex(in[i + 2]) >= 0)
			{
				out.push_back(static_cast<char>(hex(in[i + 1]) * 16 + hex(in[i + 2])));
				i += 2;
			}
			else
			{
				out.push_back(in[i]);
			}
		}
		return out;
	}

	static inline const uint8_t* GetAccessorData(const tg3_model& inModel, int inIndex, size_t& outStride, size_t& outCount)
	{
		const tg3_accessor& accessor = inModel.accessors[inIndex];
		outCount = static_cast<size_t>(accessor.count);

		if (accessor.buffer_view < 0)
		{
			outStride = 0;
			return nullptr;
		}

		const tg3_buffer_view& view = inModel.buffer_views[accessor.buffer_view];
		const tg3_buffer& buffer = inModel.buffers[view.buffer];

		int32_t stride = tg3_accessor_byte_stride(&accessor, &view);
		outStride = stride > 0 ? static_cast<size_t>(stride) : 0;

		return buffer.data.data + view.byte_offset + accessor.byte_offset;
	}

	static inline bool IsFloatAccessor(const tg3_model& model, int accessorIdx, int32_t type)
	{
		if (accessorIdx < 0 || accessorIdx >= static_cast<int>(model.accessors_count)) return false;
		const tg3_accessor& a = model.accessors[accessorIdx];
		return a.component_type == TG3_COMPONENT_TYPE_FLOAT && a.type == type && !a.normalized;
	}

	static inline int TextureToImage(const tg3_model& model, int textureIdx)
	{
		if (textureIdx < 0 || textureIdx >= static_cast<int>(model.textures_count)) return -1;
		return model.textures[textureIdx].source;
	}

	static inline bool ReadIndices(const tg3_model& model, int accessorIdx, std::vector<uint32_t>& outIndices)
	{
		const tg3_accessor& acc = model.accessors[accessorIdx];
		size_t stride = 0, count = 0;
		const uint8_t* data = GetAccessorData(model, accessorIdx, stride, count);
		if (!data) return false;

		outIndices.resize(count);
		switch (acc.component_type)
		{
		case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
			for (size_t k = 0; k < count; ++k) outIndices[k] = data[k * stride];
			break;
		case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
			for (size_t k = 0; k < count; ++k)
			{
				uint16_t v;
				std::memcpy(&v, data + k * stride, sizeof(v));
				outIndices[k] = v;
			}
			break;
		case TG3_COMPONENT_TYPE_UNSIGNED_INT:
			for (size_t k = 0; k < count; ++k) std::memcpy(&outIndices[k], data + k * stride, sizeof(uint32_t));
			break;
		default:
			outIndices.clear();
			return false;
		}
		return true;
	}

	// Area-weighted smooth normals, used when the file has no NORMAL attribute
	static inline void GenerateSmoothNormals(std::vector<GlTFModelData::Vertex>& vertices, const std::vector<uint32_t>& indices)
	{
		for (size_t k = 0; k + 2 < indices.size(); k += 3)
		{
			GlTFModelData::Vertex& a = vertices[indices[k]];
			GlTFModelData::Vertex& b = vertices[indices[k + 1]];
			GlTFModelData::Vertex& c = vertices[indices[k + 2]];
			const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
			a.normal += n;
			b.normal += n;
			c.normal += n;
		}
		for (auto& v : vertices)
		{
			v.normal = glm::dot(v.normal, v.normal) > 0.0f ? glm::normalize(v.normal) : glm::vec3(0.0f, 1.0f, 0.0f);
		}
	}

	static inline bool LoadGlTF(const std::string& inPath, GlTFModelData& outModelData)
	{
		using Vertex = GlTFModelData::Vertex;

		if (!std::filesystem::exists(inPath))
		{
			std::cerr << "File does not exist: " << inPath << std::endl;
			return false;
		}

		std::cout << "Loading GlTF: " << inPath << std::endl;

		tg3_model model;
		tg3_parse_options opts;
		tg3_error_stack errors;

		tg3_parse_options_init(&opts);
		tg3_error_stack_init(&errors);
		opts.images_as_is = 1;

		tg3_error_code parseResult = tg3_parse_file(&model, &errors, inPath.c_str(), inPath.size(), &opts);

		if (parseResult != TG3_OK)
		{
			std::cerr << "Error parsing glTF file, errors found: \n";
			for (uint32_t i = 0; i < errors.count; ++i)
			{
				const char* msg = errors.entries[i].message;
				std::cerr << (msg ? msg : "(null)") << std::endl;
			}
			tg3_error_stack_free(&errors);
			return false;
		}
		tg3_error_stack_free(&errors);

		outModelData = GlTFModelData{};
		outModelData.filePath = inPath;

		const std::filesystem::path imageDir = std::filesystem::path(inPath).parent_path();

		// images
		outModelData.images.resize(model.images_count);
		for (uint32_t i = 0; i < model.images_count; ++i)
		{
			const tg3_image& image = model.images[i];
			ImageData& dst = outModelData.images[i];

			if (image.buffer_view >= 0)
			{
				// embedded in a binary buffer (GLB)
				const tg3_buffer_view& view = model.buffer_views[image.buffer_view];
				const tg3_buffer& buffer = model.buffers[view.buffer];
				if (view.byte_offset + view.byte_length > buffer.data.count)
				{
					std::cerr << "Image " << i << " buffer view is out of range, skipping.\n";
					continue;
				}
				const uint8_t* bytes = buffer.data.data + view.byte_offset;
				FileManager::ReadImageMemory(std::span<const std::uint8_t>(bytes, static_cast<size_t>(view.byte_length)), dst);
			}
			else if (image.as_is && image.image.data != nullptr && image.image.count > 0)
			{
				// raw encoded bytes already supplied by the parser
				FileManager::ReadImageMemory(std::span<const std::uint8_t>(image.image.data, static_cast<size_t>(image.image.count)), dst);
			}
			else if (image.uri.data != nullptr && image.uri.len > 0)
			{
				const std::string uri = DecodeURI(ToString(image.uri));
				if (uri.rfind("data:", 0) == 0)
				{
					std::cerr << "Image " << i << " is a data URI the parser did not resolve, skipping.\n";
					continue;
				}
				const std::filesystem::path imagePath = imageDir / std::filesystem::path(uri);
				FileManager::ReadImageFile(imagePath.string().c_str(), dst, false);
			}
		}

		// materials
		outModelData.materials.resize(model.materials_count);
		for (uint32_t i = 0; i < model.materials_count; ++i)
		{
			const tg3_material& src = model.materials[i];
			const tg3_pbr_metallic_roughness& pbr = src.pbr_metallic_roughness;
			GlTFModelData::Material& dst = outModelData.materials[i];

			dst.name = ToString(src.name);
			dst.baseColorFactor = glm::vec4(static_cast<float>(pbr.base_color_factor[0]), static_cast<float>(pbr.base_color_factor[1]), static_cast<float>(pbr.base_color_factor[2]), static_cast<float>(pbr.base_color_factor[3]));
			dst.metallicFactor = static_cast<float>(pbr.metallic_factor);
			dst.roughnessFactor = static_cast<float>(pbr.roughness_factor);

			// texture indices are resolved to image indices
			dst.baseColorTextureIdx = TextureToImage(model, pbr.base_color_texture.index);
			dst.metallicRoughnessTextureIdx = TextureToImage(model, pbr.metallic_roughness_texture.index);
			dst.metallicRoughnessImageIdx = dst.metallicRoughnessTextureIdx;
			dst.normalTextureIdx = TextureToImage(model, src.normal_texture.index);
			dst.emissiveTextureIdx = TextureToImage(model, src.emissive_texture.index);
		}

		// meshes
		outModelData.meshes.resize(model.meshes_count);
		for (uint32_t i = 0; i < model.meshes_count; ++i)
		{
			const tg3_mesh& tgMesh = model.meshes[i];
			outModelData.meshes[i].name = ToString(tgMesh.name);

			for (uint32_t j = 0; j < tgMesh.primitives_count; ++j)
			{
				const tg3_primitive& tgPrimitive = tgMesh.primitives[j];

				if (tgPrimitive.mode != -1 && tgPrimitive.mode != TG3_MODE_TRIANGLES)
				{
					std::cerr << "Mesh " << i << " primitive " << j << ": only TRIANGLES mode is supported, skipping.\n";
					continue;
				}

				int posAccessor = -1, texCoordAccessor = -1, normalAccessor = -1, tangentAccessor = -1;
				for (uint32_t k = 0; k < tgPrimitive.attributes_count; ++k)
				{
					const tg3_str_int_pair& attribute = tgPrimitive.attributes[k];
					if (tg3_str_equals_cstr(attribute.key, "POSITION")) posAccessor = attribute.value;
					else if (tg3_str_equals_cstr(attribute.key, "TEXCOORD_0")) texCoordAccessor = attribute.value;
					else if (tg3_str_equals_cstr(attribute.key, "NORMAL")) normalAccessor = attribute.value;
					else if (tg3_str_equals_cstr(attribute.key, "TANGENT")) tangentAccessor = attribute.value;
				}

				if (posAccessor < 0)
				{
					std::cerr << "Primitive missing 'POSITION' attribute." << std::endl;
					continue;
				}
				if (!IsFloatAccessor(model, posAccessor, TG3_TYPE_VEC3))
				{
					std::cerr << "'POSITION' must be a float VEC3 accessor, skipping primitive.\n";
					continue;
				}

				size_t posStride = 0, posCount = 0;
				const uint8_t* posData = GetAccessorData(model, posAccessor, posStride, posCount);
				if (!posData)
				{
					std::cerr << "'POSITION' accessor has no backing buffer view, skipping primitive.\n";
					continue;
				}

				// Optional attributes
				size_t uvStride = 0, uvCount = 0;
				const uint8_t* uvData = nullptr;
				if (IsFloatAccessor(model, texCoordAccessor, TG3_TYPE_VEC2))
				{
					uvData = GetAccessorData(model, texCoordAccessor, uvStride, uvCount);
					if (uvCount < posCount) uvData = nullptr;
				}
				else if (texCoordAccessor >= 0)
				{
					std::cerr << "TEXCOORD_0 is not float VEC2 (quantized UVs are unsupported), ignoring.\n";
				}

				size_t normalStride = 0, normalCount = 0;
				const uint8_t* normalData = nullptr;
				if (IsFloatAccessor(model, normalAccessor, TG3_TYPE_VEC3))
				{
					normalData = GetAccessorData(model, normalAccessor, normalStride, normalCount);
					if (normalCount < posCount) normalData = nullptr;
				}

				size_t tangentStride = 0, tangentCount = 0;
				const uint8_t* tangentData = nullptr;
				if (IsFloatAccessor(model, tangentAccessor, TG3_TYPE_VEC4))
				{
					tangentData = GetAccessorData(model, tangentAccessor, tangentStride, tangentCount);
					if (tangentCount < posCount) tangentData = nullptr;
				}

				GlTFModelData::Primitive primitive;
				primitive.materialIndex = tgPrimitive.material;

				// vertex defaults
				primitive.vertices.resize(posCount);
				for (size_t v = 0; v < posCount; ++v)
				{
					Vertex& vert = primitive.vertices[v];
					std::memcpy(&vert.position, posData + v * posStride, sizeof(glm::vec3));
					if (uvData)      std::memcpy(&vert.texCoord, uvData + v * uvStride, sizeof(glm::vec2));
					if (normalData)  std::memcpy(&vert.normal, normalData + v * normalStride, sizeof(glm::vec3));
					if (tangentData) std::memcpy(&vert.tangent, tangentData + v * tangentStride, sizeof(glm::vec4));
				}

				// Indices, normalized to uint32
				if (tgPrimitive.indices >= 0)
				{
					if (!ReadIndices(model, tgPrimitive.indices, primitive.indices))
					{
						std::cerr << "Failed to read indices (unsupported type or missing buffer view), skipping primitive.\n";
						continue;
					}
				}
				else
				{
					// no index buffer: non-indexed triangle list
					primitive.indices.resize(posCount);
					for (size_t v = 0; v < posCount; ++v) primitive.indices[v] = static_cast<uint32_t>(v);
				}

				if (primitive.indices.size() % 3 != 0)
				{
					std::cerr << "Index count is not a multiple of 3, skipping primitive.\n";
					continue;
				}
				if (std::any_of(primitive.indices.begin(), primitive.indices.end(),
					[posCount](uint32_t idx) { return idx >= posCount; }))
				{
					std::cerr << "Index out of range of vertex count, skipping primitive.\n";
					continue;
				}

				if (!normalData)
				{
					GenerateSmoothNormals(primitive.vertices, primitive.indices);
				}

				outModelData.meshes[i].primitives.push_back(std::move(primitive));
			}
		}

		// nodes
		outModelData.nodes.resize(model.nodes_count);
		std::vector<bool> hasParent(model.nodes_count, false);
		for (uint32_t i = 0; i < model.nodes_count; ++i)
		{
			const tg3_node& src = model.nodes[i];
			GlTFModelData::Node& dst = outModelData.nodes[i];

			dst.name = ToString(src.name);
			dst.meshIndex = src.mesh;

			if (src.has_matrix)
			{
				// glTF matrices are column-major, same as glm
				float* m = glm::value_ptr(dst.localTransform);
				for (int k = 0; k < 16; ++k) m[k] = static_cast<float>(src.matrix[k]);
			}
			else
			{
				const glm::vec3 t(static_cast<float>(src.translation[0]), static_cast<float>(src.translation[1]), static_cast<float>(src.translation[2]));
				// glTF quaternion order is (x, y, z, w)
				const glm::quat r(static_cast<float>(src.rotation[3]), static_cast<float>(src.rotation[0]), static_cast<float>(src.rotation[1]), static_cast<float>(src.rotation[2]));
				const glm::vec3 s(static_cast<float>(src.scale[0]), static_cast<float>(src.scale[1]), static_cast<float>(src.scale[2]));
				dst.localTransform = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
			}

			dst.childrenIndices.reserve(src.children_count);
			for (uint32_t c = 0; c < src.children_count; ++c)
			{
				const int child = src.children[c];
				dst.childrenIndices.push_back(child);
				if (child >= 0 && child < static_cast<int>(model.nodes_count)) hasParent[child] = true;
			}
		}

		// root nodes
		if (model.scenes_count > 0)
		{
			const int sceneIdx = (model.default_scene >= 0 && model.default_scene < static_cast<int>(model.scenes_count)) ? model.default_scene : 0;
			const tg3_scene& scene = model.scenes[sceneIdx];
			for (uint32_t i = 0; i < scene.nodes_count; ++i) outModelData.rootNodes.push_back(scene.nodes[i]);
		}
		else
		{
			// no scenes: every parentless node is a root
			for (uint32_t i = 0; i < model.nodes_count; ++i)
			{
				if (!hasParent[i]) outModelData.rootNodes.push_back(static_cast<int>(i));
			}
		}

		tg3_model_free(&model);

		return true;
	}

	static inline void PrintGlTFSummary(const GlTFModelData& d)
	{
		size_t totalVerts = 0, totalTris = 0, totalPrims = 0;
		for (const auto& m : d.meshes)
		{
			for (const auto& p : m.primitives)
			{
				++totalPrims;
				totalVerts += p.vertices.size();
				totalTris += p.indices.size() / 3;
			}
		}

		std::cout << "=== glTF summary: " << d.filePath << " ===\n"
			<< "images: " << d.images.size() << ", materials: " << d.materials.size()
			<< ", meshes: " << d.meshes.size() << " (" << totalPrims << " primitives)"
			<< ", nodes: " << d.nodes.size() << ", root nodes: " << d.rootNodes.size() << "\n"
			<< "total vertices: " << totalVerts << ", total triangles: " << totalTris << "\n";

		// Materials (and how many material slots reference each image)
		std::vector<int> imageUsers(d.images.size(), 0);
		auto touch = [&](int idx) { if (idx >= 0 && idx < static_cast<int>(imageUsers.size())) ++imageUsers[idx]; };

		std::cout << "\n-- materials --\n";
		for (size_t i = 0; i < d.materials.size(); ++i)
		{
			const auto& m = d.materials[i];
			std::cout << "[" << i << "] \"" << m.name << "\""
				<< " baseColor=(" << m.baseColorFactor.r << ", " << m.baseColorFactor.g << ", " << m.baseColorFactor.b << ", " << m.baseColorFactor.a << ")"
				<< " metallic=" << m.metallicFactor << " roughness=" << m.roughnessFactor
				<< "\n     images: base=" << m.baseColorTextureIdx << " normal=" << m.normalTextureIdx
				<< " metalRough=" << m.metallicRoughnessTextureIdx << " emissive=" << m.emissiveTextureIdx << "\n";
			touch(m.baseColorTextureIdx);
			touch(m.normalTextureIdx);
			touch(m.metallicRoughnessTextureIdx);
			touch(m.emissiveTextureIdx);
		}

		std::cout << "\n-- images --\n";
		for (size_t i = 0; i < imageUsers.size(); ++i)
		{
			std::cout << "[" << i << "] used by " << imageUsers[i] << " material slot(s)" << (imageUsers[i] == 0 ? "  (UNUSED)" : "") << "\n";
		}

		// Meshes
		std::cout << "\n-- meshes --\n";
		for (size_t i = 0; i < d.meshes.size(); ++i)
		{
			const auto& mesh = d.meshes[i];
			std::cout << "[" << i << "] \"" << mesh.name << "\" (" << mesh.primitives.size() << " primitives)\n";
			for (size_t j = 0; j < mesh.primitives.size(); ++j)
			{
				const auto& p = mesh.primitives[j];
				glm::vec3 bmin(std::numeric_limits<float>::max());
				glm::vec3 bmax(std::numeric_limits<float>::lowest());
				for (const auto& v : p.vertices) { bmin = glm::min(bmin, v.position); bmax = glm::max(bmax, v.position); }
				std::cout << "     prim " << j << ": " << p.vertices.size() << " verts, " << p.indices.size() / 3 << " tris, material=" << p.materialIndex;
				if (!p.vertices.empty())
				{
					std::cout << ", bounds=(" << bmin.x << ", " << bmin.y << ", " << bmin.z << ") to ("
						<< bmax.x << ", " << bmax.y << ", " << bmax.z << ")";
				}
				std::cout << "\n";
			}
		}

		// Node hierarchy
		std::cout << "\n-- node hierarchy --\n";
		auto printNode = [&](auto&& self, int idx, int depth) -> void
			{
				if (idx < 0 || idx >= static_cast<int>(d.nodes.size()) || depth > 64) return; // depth guard against cycles
				const auto& n = d.nodes[idx];
				std::cout << std::string(static_cast<size_t>(depth) * 2, ' ') << "- [" << idx << "] \"" << n.name << "\"";
				if (n.meshIndex >= 0) std::cout << " mesh=" << n.meshIndex;
				const glm::vec3 t(n.localTransform[3]);
				std::cout << " pos=(" << t.x << ", " << t.y << ", " << t.z << ")\n";
				for (int child : n.childrenIndices) self(self, child, depth + 1);
			};
		for (int root : d.rootNodes) printNode(printNode, root, 0);

		std::cout << "=== end summary ===" << std::endl;
	}
}