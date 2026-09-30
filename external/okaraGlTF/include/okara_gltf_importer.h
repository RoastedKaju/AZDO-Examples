#include <print>
#include <string>
#include <fstream>
#include <array>
#include <vector>
#include <cstddef>
#include <cassert>

#include "cJSON.h"

// TODO: implement get failure reason function for this library
// TODO: add GlTF support
// TODO: read values as double to support larger scenes
// TODO: make copy binary data into a function

namespace okara::gltf
{
    using Matrix4x4 = std::array<float, 16>;

    static constexpr std::uint32_t GLB_MAGIC = 0x46546C67;
    static constexpr std::uint32_t GLB_VERSION = 2;
    static constexpr std::uint32_t JSON_CHUNK = 0x4E4F534A;
    static constexpr std::uint32_t BIN_CHUNK = 0x004E4942;
    // clang-format off
    static constexpr Matrix4x4 IDENTITY_MATRIX = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    // clang-format on

    static inline Matrix4x4 Multiply(const Matrix4x4 &a, const Matrix4x4 &b)
    {
        Matrix4x4 result{};
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    sum += a[k * 4 + row] * b[col * 4 + k];
                }
                result[col * 4 + row] = sum;
            }
        return result;
    }

    static inline Matrix4x4 ComposeTranslationRotationScale(const std::array<float, 3> &t, const std::array<float, 4> &q, const std::array<float, 3> &s)
    {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        Matrix4x4 m = IDENTITY_MATRIX;

        // rotation columns, each scaled by the matching scale component
        m[0] = (1 - 2 * (y * y + z * z)) * s[0];
        m[1] = (2 * (x * y + z * w)) * s[0];
        m[2] = (2 * (x * z - y * w)) * s[0];

        m[4] = (2 * (x * y - z * w)) * s[1];
        m[5] = (1 - 2 * (x * x + z * z)) * s[1];
        m[6] = (2 * (y * z + x * w)) * s[1];

        m[8] = (2 * (x * z + y * w)) * s[2];
        m[9] = (2 * (y * z - x * w)) * s[2];
        m[10] = (1 - 2 * (x * x + y * y)) * s[2];

        // translation
        m[12] = t[0];
        m[13] = t[1];
        m[14] = t[2];

        return m;
    }

    struct Header
    {
        std::uint32_t magic{};
        std::uint32_t version{};
        std::uint32_t length{};
    };

    struct ChunkHeader
    {
        std::uint32_t length{};
        std::uint32_t type{};
    };

    struct BufferView
    {
        std::int32_t buffer{-1};      // index into buffer array
        std::size_t byte_offset{0};   // byte offset into buffer
        std::size_t byte_length{0};   // length of buffer view in bytes
        std::uint32_t byte_stride{0}; // stride between elements
    };

    struct Accessor
    {
        std::int32_t buffer_view{-1};  // index into buffer view array
        std::size_t byte_offset{0};    // byte offset relative to buffer view
        std::int32_t component_type{}; // float (5126), unsigned short (5123)
        std::size_t count{};           // number of elements in accessor
        std::string type{};            // "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"
    };

#pragma pack(push, 1)
    struct Vertex
    {
        float position[3]{0.0f, 0.0f, 0.0f};
        float normal[3]{0.0f, 0.0f, 0.0f};
        float tangent[4]{0.0f, 0.0f, 0.0f, 1.0f};
        float uv0[2]{0.0f, 0.0f};
        float uv1[2]{0.0f, 0.0f};
        float color0[4]{1.0f, 1.0f, 1.0f, 1.0f};
        float joints0[4]{0.0f, 0.0f, 0.0f, 0.0f};
        float weights0[4]{0.0f, 0.0f, 0.0f, 0.0f};
    };
#pragma pack(pop)

    struct Material
    {
        std::string name{};

        float base_color_factor[4]{1.0f, 1.0f, 1.0f, 1.0f}; // RGBA
        float metallic_factor{1.0f};
        float roughness_factor{1.0f};

        std::int32_t base_color_texture{-1};
        std::int32_t metallic_roughness_texture{-1};
        std::int32_t normal_texture{-1};
        std::int32_t occlusion_texture{-1};
        std::int32_t emissive_texture{-1};

        float emissive_factor[3]{0.0f, 0.0f, 0.0f};

        // material render flags
        bool double_sided{false};
        std::string alpha_mode{"OPAQUE"}; // "OPAQUE", "MASK", or "BLEND"
        float alpha_cut_off{0.5f};
    };

    struct Image
    {
        std::string name{};
        std::string uri{};
        std::string mime_type{};
        std::vector<std::byte> data{};
        std::int32_t buffer_view{-1};
    };

    struct Sampler
    {
        std::string name{};
        int32_t mag_filter{9729}; // default: LINEAR (9729)
        int32_t min_filter{9987}; // default: LINEAR_MIPMAP_LINEAR (9987)
        int32_t wrap_s{10497};    // default: REPEAT (10497)
        int32_t wrap_t{10497};    // default: REPEAT (10497)
    };

    struct Texture
    {
        std::string name{};
        std::int32_t source{-1};
        std::int32_t sampler{-1};
    };

    enum class PrimitiveRenderMode
    {
        kPoints = 0,
        kLines = 1,
        kLineLoop = 2,
        kLineStrip = 3,
        kTriangles = 4,
        kTriangleStrip = 5,
        kTriangleFan = 6
    };

    struct Primitive
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        std::int32_t position_accessor{-1};
        std::int32_t normal_accessor{-1};
        std::int32_t tangent_accessor{-1};
        std::int32_t texcoord0_accessor{-1};
        std::int32_t texcoord1_accessor{-1};
        std::int32_t color0_accessor{-1};
        std::int32_t joints0_accessor{-1};
        std::int32_t weights0_accessor{-1};

        // Index Buffer Accessor (-1 for non-indexed geometry)
        std::int32_t index_accessor{-1};

        std::int32_t material{-1};
        PrimitiveRenderMode mode{PrimitiveRenderMode::kTriangles};
    };

    /// @brief Unique mesh containing a set of renderable primitives, think of this as raw untransformed geometry
    struct Mesh
    {
        std::vector<Primitive> primitives;
    };

    /// @brief An instance that references to a mesh and has a transform which is inherited via its node hierarchy
    struct MeshInstance
    {
        std::int32_t mesh{-1};
        Matrix4x4 world_transform{IDENTITY_MATRIX};
    };

    struct Node
    {
        std::vector<int> children;

        std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f};
        std::array<float, 3> translation{0.0f, 0.0f, 0.0f};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};

        bool has_matrix{false};
        Matrix4x4 matrix = IDENTITY_MATRIX;

        int mesh{-1};
    };

    struct Scene
    {
        std::string name;
        std::vector<int> nodes;
    };

    class Loader
    {
    public:
        bool LoadScene(const std::string &path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                std::print("Failed to open file: {}.\n", path);
                return false;
            }

            // read the first 12-bytes
            file.read(reinterpret_cast<char *>(&header_), sizeof(Header));

            if (!file)
            {
                std::printf("Failed to read GLB header.\n");
                return false;
            }

            // check if file is GLB
            if (header_.magic != GLB_MAGIC)
            {
                std::print("Invalid GLB file.\n");
                return false;
            }
            if (header_.version != GLB_VERSION)
            {
                std::print("Unsupported GLB version.\n");
                return false;
            }

            // verify file size
            file.seekg(0, std::ios::end);
            const auto file_size = file.tellg();
            if (file_size == std::streampos(-1))
            {
                std::print("Failed to determine file size.\n");
                return false;
            }

            // return the position back to 12 bytes
            file.seekg(sizeof(Header), std::ios::beg);

            // print header data
            std::print("Magic: 0x{:08X}, Version: {}, Length: {}\n", header_.magic, header_.version, header_.length);

            // TODO: length variable in header is only 4 bytes but stream position is 64 bits
            if (header_.length != file_size)
            {
                std::print("File size does not match value provided by header length.\n");
                return false;
            }

            // read the first chunk header
            ChunkHeader chunk_header{};
            // for a valid glTF 2.0 GLB, the first chunk must be JSON
            // type: 0x4E4F534A which is JSON in ASCII
            file.read(reinterpret_cast<char *>(&chunk_header), sizeof(ChunkHeader));
            if (!file)
            {
                std::print("Failed to read chunk data.\n");
                return false;
            }

            // read JSON chunk
            std::vector<char> json_data(chunk_header.length);
            file.read(json_data.data(), json_data.size());
            if (!file)
            {
                std::print("Failed to read JSON chunk.\n");
                return false;
            }

            // validate
            if (chunk_header.type != JSON_CHUNK)
            {
                std::print("First GLB chunk is not a JSON chunk.\n");
                return false;
            }
            // print chunk header data
            std::print("Chunk Header Type: {:08X}, Length: {}\n", chunk_header.type, chunk_header.length);

            cJSON *root = cJSON_ParseWithLength(json_data.data(), json_data.size());
            if (!root)
            {
                std::print("Failed to parse GLB JSON.\n");
                return false;
            }
            // get asset object
            cJSON *asset = cJSON_GetObjectItemCaseSensitive(root, "asset");
            if (!cJSON_IsObject(asset))
            {
                std::print("GLB JSON does not contain a valid asset object.\n");
                cJSON_Delete(root);
                return false;
            }
            // get version
            cJSON *version = cJSON_GetObjectItemCaseSensitive(asset, "version");
            if (!cJSON_IsString(version))
            {
                std::print("GLB asset does not contain a valid version.\n");
                cJSON_Delete(root);
                return false;
            }

            if (std::strcmp(version->valuestring, "2.0") != 0)
            {
                std::print("Unsupported glTF version: {}\n", version->valuestring);
                cJSON_Delete(root);
                return false;
            }

            // the relationship required for getting geometry out of glTF is:
            // buffers -> bufferViews -> accessors -> meshes -> primitives
            // binary chunk is just raw bytes, buffer describes a region, view describes how to interpret it, accessors are attributes

            // buffers
            if (!ParseBuffers(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // buffer views
            if (!ParseBufferViews(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // accessors know how to interpert the data where buffer views point
            if (!ParseAccessors(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // inspect meshes
            if (!ParseMeshes(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // nodes
            if (!ParseNodes(root))
            {
                cJSON_Delete(root);
                return false;
            }
            // scenes
            if (!ParseScenes(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // materials
            if (!ParseMaterials(root))
            {
                cJSON_Delete(root);
                return false;
            }
            // textures
            if (!ParseTextures(root))
            {
                cJSON_Delete(root);
                return false;
            }
            // samplers
            if (!ParseSamplers(root))
            {
                cJSON_Delete(root);
                return false;
            }
            // images
            if (!ParseImages(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // by this point we have all of our meta data
            cJSON_Delete(root);

            // read binary chunk header
            ChunkHeader bin_chunk_header{};
            file.read(reinterpret_cast<char *>(&bin_chunk_header), sizeof(ChunkHeader));
            if (!file)
            {
                std::print("Failed to read binary chunk data.\n");
                return false;
            }
            if (bin_chunk_header.type != BIN_CHUNK)
            {
                std::print("Second GLB chunk is not a BIN chunk.\n");
                return false;
            }
            std::print("Binary Chunk Header Type: {:08X}, Length: {}\n", bin_chunk_header.type, bin_chunk_header.length);

            // read binary data
            binary_data_.clear();
            binary_data_.resize(bin_chunk_header.length);
            file.read(reinterpret_cast<char *>(binary_data_.data()), binary_data_.size());
            if (!file)
            {
                std::print("Failed to load binary chunk.\n");
                return false;
            }
            std::print("Read {} bytes of binary data.\n", binary_data_.size());

            // reads vertices, indices and fills up the mesh structure
            for (Mesh &mesh : meshes_)
            {
                for (Primitive &prim : mesh.primitives)
                {
                    PopulatePrimitiveData(prim);
                }
            }

            // copy images data
            for (Image &image : images_)
            {
                if (image.buffer_view >= 0)
                {
                    CopyImageData(image);
                }
            }

            // traverse
            mesh_instances_.clear();
            const Scene &scene = scenes_[default_scene_];
            for (const auto node : scene.nodes)
            {
                TraverseNode(node, IDENTITY_MATRIX);
            }

            // NOTE: print mesh data
            for (const MeshInstance &instance : mesh_instances_)
            {
                const Mesh &mesh = meshes_[instance.mesh];

                std::print("Instance -> Mesh {}\n", instance.mesh);
                std::print("\tPrimitives: {}\n", mesh.primitives.size());

                for (std::size_t i = 0; i < mesh.primitives.size(); ++i)
                {
                    const Primitive &primitive = mesh.primitives[i];

                    std::print("\t\tPrimitive {}\n", i);
                    std::print("\t\t\tVertices: {}\n", primitive.vertices.size());
                    std::print("\t\t\tIndices: {}\n", primitive.indices.size());
                    std::print("\t\t\tMaterial: {}\n", primitive.material);
                }
            }

            // release the temperory data
            binary_data_.clear();
            binary_data_.shrink_to_fit();

            return true;
        }

    private:
        /// @brief Iterates over buffers array, each buffer is region.
        /// @param root JSON object.
        /// @return True if successful.
        bool ParseBuffers(cJSON *root)
        {
            cJSON *buffers = cJSON_GetObjectItemCaseSensitive(root, "buffers");
            if (!cJSON_IsArray(buffers))
            {
                std::print("GLB does not contain a valid buffers array.\n");
                return false;
            }

            std::print("Buffers: {}\n", cJSON_GetArraySize(buffers));

            cJSON *buffer = nullptr;
            cJSON_ArrayForEach(buffer, buffers)
            {
                cJSON *byte_length = cJSON_GetObjectItem(buffer, "byteLength");
                if (!cJSON_IsNumber(byte_length))
                {
                    std::print("Buffer is missing 'byteLength'.\n");
                    return false;
                }

                std::print("Byte length: {}\n", byte_length->valueint);
            }

            return true;
        }

        /// @brief Buffer views describes how to interpret the data inside a buffer region.
        /// @param root JSON object.
        /// @return True if successful.
        bool ParseBufferViews(cJSON *root)
        {
            cJSON *buffer_views = cJSON_GetObjectItemCaseSensitive(root, "bufferViews");
            if (!cJSON_IsArray(buffer_views))
            {
                std::print("GLB does not contain a valid buffer views array.\n");
                return false;
            }

            buffer_views_.clear();
            buffer_views_.reserve(cJSON_GetArraySize(buffer_views));

            std::print("Buffer views: {}\n", cJSON_GetArraySize(buffer_views));

            cJSON *buffer_view = nullptr;
            cJSON_ArrayForEach(buffer_view, buffer_views)
            {
                cJSON *buffer = cJSON_GetObjectItemCaseSensitive(buffer_view, "buffer");
                cJSON *byte_offset = cJSON_GetObjectItemCaseSensitive(buffer_view, "byteOffset");
                cJSON *byte_length = cJSON_GetObjectItemCaseSensitive(buffer_view, "byteLength");
                cJSON *byte_stride = cJSON_GetObjectItemCaseSensitive(buffer_view, "byteStride");
                if (!cJSON_IsNumber(buffer) || !cJSON_IsNumber(byte_length))
                {
                    std::print("Invalid buffer view.\n");
                    return false;
                }
                // offset is an optional field, default value is 0
                const int offset = cJSON_IsNumber(byte_offset) ? byte_offset->valueint : 0;
                const int stride = cJSON_IsNumber(byte_stride) ? byte_stride->valueint : 0;
                std::print("Buffer: {}, Offset: {}, Length: {}, Stride: {}\n", buffer->valueint, offset, byte_length->valueint, stride);

                BufferView result{};
                result.buffer = static_cast<std::uint32_t>(buffer->valueint);
                result.byte_offset = static_cast<std::size_t>(offset);
                result.byte_length = static_cast<std::size_t>(byte_length->valueint);
                result.byte_stride = static_cast<std::uint32_t>(stride);

                buffer_views_.push_back(result);
            }

            return true;
        }

        /// @brief Accessors define how the data is laid out where the buffer view points.
        /// @param root JSON object.
        /// @return True if successful.
        bool ParseAccessors(cJSON *root)
        {
            cJSON *accessors = cJSON_GetObjectItemCaseSensitive(root, "accessors");
            if (!cJSON_IsArray(accessors))
            {
                std::print("GLB does not contain a valid accessor array.\n");
                return false;
            }
            accessors_.clear();
            accessors_.reserve(cJSON_GetArraySize(accessors));

            std::print("Accessors: {}\n", cJSON_GetArraySize(accessors));

            cJSON *accessor = nullptr;
            cJSON_ArrayForEach(accessor, accessors)
            {
                cJSON *buffer_view = cJSON_GetObjectItemCaseSensitive(accessor, "bufferView");
                cJSON *byte_offset = cJSON_GetObjectItemCaseSensitive(accessor, "byteOffset");
                cJSON *component_type = cJSON_GetObjectItemCaseSensitive(accessor, "componentType");
                cJSON *count = cJSON_GetObjectItemCaseSensitive(accessor, "count");
                cJSON *type = cJSON_GetObjectItemCaseSensitive(accessor, "type");

                if (!cJSON_IsNumber(component_type) || !cJSON_IsNumber(count) || !cJSON_IsString(type))
                {
                    std::print("Invalid accessor.\n");
                    return false;
                }

                // optional fields
                const int view = cJSON_IsNumber(buffer_view) ? buffer_view->valueint : -1;
                const int offset = cJSON_IsNumber(byte_offset) ? byte_offset->valueint : 0;

                std::print("Buffer View: {}, Offset: {}, Component Type: {}, Count: {}, Type: {}\n", view, offset, component_type->valueint, count->valueint, type->valuestring);

                Accessor result{};
                result.buffer_view = static_cast<std::int32_t>(view);
                result.byte_offset = static_cast<std::size_t>(offset);
                result.component_type = static_cast<std::int32_t>(component_type->valueint);
                result.count = static_cast<std::size_t>(count->valueint);
                result.type = type->valuestring;

                accessors_.push_back(result);
            }

            return true;
        }

        /// @brief
        /// @param root
        /// @return
        bool ParseMeshes(cJSON *root)
        {
            cJSON *meshes_json = cJSON_GetObjectItemCaseSensitive(root, "meshes");
            if (!cJSON_IsArray(meshes_json))
            {
                std::print("GLB does not contain a valid meshes array.\n");
                return false;
            }

            std::print("Meshes: {}\n", cJSON_GetArraySize(meshes_json));
            cJSON *mesh_json = nullptr;
            cJSON_ArrayForEach(mesh_json, meshes_json)
            {
                Mesh mesh_result{};

                cJSON *primitives_json = cJSON_GetObjectItemCaseSensitive(mesh_json, "primitives");
                if (!cJSON_IsArray(primitives_json))
                {
                    std::print("Mesh does not contain a valid primitives array.\n");
                    return false;
                }
                std::print("Primitives: {}\n", cJSON_GetArraySize(primitives_json));
                cJSON *primitive_json = nullptr;
                cJSON_ArrayForEach(primitive_json, primitives_json)
                {
                    cJSON *attributes_json = cJSON_GetObjectItemCaseSensitive(primitive_json, "attributes");
                    // attributes value in glTF is an object not an array
                    if (!cJSON_IsObject(attributes_json))
                    {
                        std::print("Primitive does not contain valid attributes.\n");
                        return false;
                    }

                    // helper function to get accessor id
                    auto get_accessor_id = [](cJSON *obj, const char *name) -> std::int32_t
                    {
                        cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
                        return (cJSON_IsNumber(item) && item->valueint >= 0) ? item->valueint : -1;
                    };

                    Primitive prim_result{};

                    prim_result.position_accessor = get_accessor_id(attributes_json, "POSITION");
                    prim_result.normal_accessor = get_accessor_id(attributes_json, "NORMAL");
                    prim_result.tangent_accessor = get_accessor_id(attributes_json, "TANGENT");
                    prim_result.texcoord0_accessor = get_accessor_id(attributes_json, "TEXCOORD_0");
                    prim_result.texcoord1_accessor = get_accessor_id(attributes_json, "TEXCOORD_1");
                    prim_result.color0_accessor = get_accessor_id(attributes_json, "COLOR_0");
                    prim_result.joints0_accessor = get_accessor_id(attributes_json, "JOINTS_0");
                    prim_result.weights0_accessor = get_accessor_id(attributes_json, "WEIGHTS_0");

                    if (prim_result.position_accessor == -1)
                    {
                        std::print("Primitive missing required POSITION attribute.\n");
                        return false;
                    }

                    cJSON *indices = cJSON_GetObjectItemCaseSensitive(primitive_json, "indices");
                    prim_result.index_accessor = (cJSON_IsNumber(indices) && indices->valueint >= 0) ? indices->valueint : -1;

                    cJSON *mode = cJSON_GetObjectItemCaseSensitive(primitive_json, "mode");
                    prim_result.mode = static_cast<PrimitiveRenderMode>((cJSON_IsNumber(mode) && mode->valueint >= 0) ? mode->valueint : 4);

                    // this connects a primitive to our materials array
                    cJSON *material = cJSON_GetObjectItemCaseSensitive(primitive_json, "material");
                    prim_result.material = (cJSON_IsNumber(material) && material->valueint >= 0) ? material->valueint : -1;

                    mesh_result.primitives.push_back(std::move(prim_result));
                }

                meshes_.push_back(std::move(mesh_result));
            }
            return true;
        }

        /// @brief Materials define the properties of a surface, things like metallic, roughness and index to textures.
        /// @param root JSON object.
        /// @return True if successful.
        bool ParseMaterials(cJSON *root)
        {
            cJSON *materials = cJSON_GetObjectItemCaseSensitive(root, "materials");
            if (!cJSON_IsArray(materials))
            {
                // missing materials array is also valid in gltf spec
                return true;
            }

            materials_.clear();
            materials_.reserve(cJSON_GetArraySize(materials));

            // helper to extract gltf texture index from texture object
            auto parse_texture_index = [](cJSON *parent, const char *key) -> int32_t
            {
                cJSON *tex_obj = cJSON_GetObjectItemCaseSensitive(parent, key);
                if (cJSON_IsObject(tex_obj))
                {
                    cJSON *index = cJSON_GetObjectItemCaseSensitive(tex_obj, "index");
                    if (cJSON_IsNumber(index))
                    {
                        return index->valueint;
                    }
                }
                return -1;
            };

            cJSON *material = nullptr;
            cJSON_ArrayForEach(material, materials)
            {
                Material result{};

                // name
                cJSON *name = cJSON_GetObjectItemCaseSensitive(material, "name");
                if (cJSON_IsString(name))
                {
                    result.name = name->valuestring;
                }
                // metallic and roughness
                cJSON *pbr = cJSON_GetObjectItemCaseSensitive(material, "pbrMetallicRoughness");
                if (cJSON_IsObject(pbr))
                {
                    cJSON *metallic = cJSON_GetObjectItemCaseSensitive(pbr, "metallicFactor");
                    if (cJSON_IsNumber(metallic))
                    {
                        result.metallic_factor = static_cast<float>(metallic->valuedouble);
                    }
                    cJSON *roughness = cJSON_GetObjectItemCaseSensitive(pbr, "roughnessFactor");
                    if (cJSON_IsNumber(roughness))
                    {
                        result.roughness_factor = static_cast<float>(roughness->valuedouble);
                    }

                    result.base_color_texture = parse_texture_index(pbr, "baseColorTexture");
                    result.metallic_roughness_texture = parse_texture_index(pbr, "metallicRoughnessTexture");

                    cJSON *base_color_factor = cJSON_GetObjectItemCaseSensitive(pbr, "baseColorFactor");
                    if (cJSON_IsArray(base_color_factor) && cJSON_GetArraySize(base_color_factor) == 4)
                    {
                        cJSON *r = cJSON_GetArrayItem(base_color_factor, 0);
                        cJSON *g = cJSON_GetArrayItem(base_color_factor, 1);
                        cJSON *b = cJSON_GetArrayItem(base_color_factor, 2);
                        cJSON *a = cJSON_GetArrayItem(base_color_factor, 3);

                        if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b) && cJSON_IsNumber(a))
                        {
                            result.base_color_factor[0] = static_cast<float>(r->valuedouble);
                            result.base_color_factor[1] = static_cast<float>(g->valuedouble);
                            result.base_color_factor[2] = static_cast<float>(b->valuedouble);
                            result.base_color_factor[3] = static_cast<float>(a->valuedouble);
                        }
                    }
                }

                // additional material textures
                result.normal_texture = parse_texture_index(material, "normalTexture");
                result.occlusion_texture = parse_texture_index(material, "occlusionTexture");
                result.emissive_texture = parse_texture_index(material, "emissiveTexture");

                // emissive factor
                cJSON *emissive_factor = cJSON_GetObjectItemCaseSensitive(material, "emissiveFactor");
                if (cJSON_IsArray(emissive_factor) && cJSON_GetArraySize(emissive_factor) == 3)
                {
                    cJSON *r = cJSON_GetArrayItem(emissive_factor, 0);
                    cJSON *g = cJSON_GetArrayItem(emissive_factor, 1);
                    cJSON *b = cJSON_GetArrayItem(emissive_factor, 2);

                    if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b))
                    {
                        result.emissive_factor[0] = static_cast<float>(r->valuedouble);
                        result.emissive_factor[1] = static_cast<float>(g->valuedouble);
                        result.emissive_factor[2] = static_cast<float>(b->valuedouble);
                    }
                }

                // render flags
                cJSON *double_sided = cJSON_GetObjectItemCaseSensitive(material, "doubleSided");
                if (cJSON_IsBool(double_sided))
                {
                    result.double_sided = cJSON_IsTrue(double_sided);
                }
                cJSON *alpha_mode = cJSON_GetObjectItemCaseSensitive(material, "alphaMode");
                if (cJSON_IsString(alpha_mode))
                {
                    result.alpha_mode = alpha_mode->valuestring;
                }
                cJSON *alpha_cutoff = cJSON_GetObjectItemCaseSensitive(material, "alphaCutoff");
                if (cJSON_IsNumber(alpha_cutoff))
                {
                    result.alpha_cut_off = static_cast<float>(alpha_cutoff->valuedouble);
                }

                materials_.push_back(std::move(result));
            }
            return true;
        }

        /// @brief Texture is an index to a source image.
        /// @param root JSON object.
        /// @return True on successful.
        bool ParseTextures(cJSON *root)
        {
            cJSON *textures = cJSON_GetObjectItemCaseSensitive(root, "textures");
            if (!cJSON_IsArray(textures))
            {
                // Missing textures array is valid gltf
                return true;
            }

            textures_.clear();
            textures_.reserve(cJSON_GetArraySize(textures));

            cJSON *texture = nullptr;
            cJSON_ArrayForEach(texture, textures)
            {
                Texture result{};

                cJSON *name = cJSON_GetObjectItemCaseSensitive(texture, "name");
                if (cJSON_IsString(name))
                {
                    result.name = name->valuestring;
                }
                cJSON *source = cJSON_GetObjectItemCaseSensitive(texture, "source");
                if (cJSON_IsNumber(source))
                {
                    result.source = source->valueint;
                }
                cJSON *sampler = cJSON_GetObjectItemCaseSensitive(texture, "sampler");
                if (cJSON_IsNumber(sampler))
                {
                    result.sampler = sampler->valueint;
                }

                textures_.push_back(std::move(result));
            }

            return true;
        }

        bool ParseSamplers(cJSON *root)
        {
            cJSON *samplers = cJSON_GetObjectItemCaseSensitive(root, "samplers");
            if (!cJSON_IsArray(samplers))
            {
                return true;
            }

            samplers_.clear();
            samplers_.reserve(cJSON_GetArraySize(samplers));

            cJSON *sampler = nullptr;
            cJSON_ArrayForEach(sampler, samplers)
            {
                Sampler result{};

                cJSON *mag = cJSON_GetObjectItemCaseSensitive(sampler, "magFilter");
                if (cJSON_IsNumber(mag))
                    result.mag_filter = mag->valueint;

                cJSON *min = cJSON_GetObjectItemCaseSensitive(sampler, "minFilter");
                if (cJSON_IsNumber(min))
                    result.min_filter = min->valueint;

                cJSON *wrap_s = cJSON_GetObjectItemCaseSensitive(sampler, "wrapS");
                if (cJSON_IsNumber(wrap_s))
                    result.wrap_s = wrap_s->valueint;

                cJSON *wrap_t = cJSON_GetObjectItemCaseSensitive(sampler, "wrapT");
                if (cJSON_IsNumber(wrap_t))
                    result.wrap_t = wrap_t->valueint;

                samplers_.push_back(result);
            }

            return true;
        }

        /// @brief Loops over source images, which points to buffer view holding image data and extracts meta-data.
        /// @param root JSON object.
        /// @return True if successful.
        bool ParseImages(cJSON *root)
        {
            cJSON *images = cJSON_GetObjectItemCaseSensitive(root, "images");
            if (!cJSON_IsArray(images))
            {
                return true;
            }

            images_.clear();
            images_.reserve(cJSON_GetArraySize(images));

            cJSON *image = nullptr;
            cJSON_ArrayForEach(image, images)
            {
                Image result{};

                cJSON *name = cJSON_GetObjectItemCaseSensitive(image, "name");
                if (cJSON_IsString(name))
                {
                    result.name = name->valuestring;
                }

                cJSON *uri = cJSON_GetObjectItemCaseSensitive(image, "uri");
                if (cJSON_IsString(uri))
                {
                    result.uri = uri->valuestring;
                }

                cJSON *buffer_view = cJSON_GetObjectItemCaseSensitive(image, "bufferView");
                if (cJSON_IsNumber(buffer_view))
                {
                    result.buffer_view = buffer_view->valueint;
                }

                cJSON *mime_type = cJSON_GetObjectItemCaseSensitive(image, "mimeType");
                if (cJSON_IsString(mime_type))
                {
                    result.mime_type = mime_type->valuestring;
                }

                images_.push_back(std::move(result));
            }
            return true;
        }

        bool ParseNodes(cJSON *root)
        {
            cJSON *nodes_json = cJSON_GetObjectItemCaseSensitive(root, "nodes");
            if (!cJSON_IsArray(nodes_json))
            {
                std::print("GLB does not contain a valid nodes array.\n");
                // a GLB can have no nodes, so we return true nevertheless.
                return true;
            }
            std::print("Nodes: {}\n", cJSON_GetArraySize(nodes_json));
            nodes_.clear();
            nodes_.reserve(cJSON_GetArraySize(nodes_json));

            cJSON *node_json = nullptr;
            cJSON_ArrayForEach(node_json, nodes_json)
            {
                Node result{};

                // mesh reference
                cJSON *mesh = cJSON_GetObjectItemCaseSensitive(node_json, "mesh");
                if (cJSON_IsNumber(mesh))
                {
                    result.mesh = mesh->valueint;
                }
                // children index
                cJSON *children = cJSON_GetObjectItemCaseSensitive(node_json, "children");
                if (cJSON_IsArray(children))
                {
                    cJSON *child = nullptr;
                    cJSON_ArrayForEach(child, children)
                    {
                        if (cJSON_IsNumber(child))
                        {
                            result.children.push_back(child->valueint);
                        }
                    }
                }
                // translation
                cJSON *translation = cJSON_GetObjectItemCaseSensitive(node_json, "translation");
                if (cJSON_IsArray(translation) && cJSON_GetArraySize(translation) == 3)
                {
                    result.translation[0] = static_cast<float>(cJSON_GetArrayItem(translation, 0)->valuedouble);
                    result.translation[1] = static_cast<float>(cJSON_GetArrayItem(translation, 1)->valuedouble);
                    result.translation[2] = static_cast<float>(cJSON_GetArrayItem(translation, 2)->valuedouble);
                }
                // rotation
                cJSON *rotation = cJSON_GetObjectItemCaseSensitive(node_json, "rotation");
                if (cJSON_IsArray(rotation) && cJSON_GetArraySize(rotation) == 4)
                {
                    result.rotation[0] = static_cast<float>(cJSON_GetArrayItem(rotation, 0)->valuedouble);
                    result.rotation[1] = static_cast<float>(cJSON_GetArrayItem(rotation, 1)->valuedouble);
                    result.rotation[2] = static_cast<float>(cJSON_GetArrayItem(rotation, 2)->valuedouble);
                    result.rotation[3] = static_cast<float>(cJSON_GetArrayItem(rotation, 3)->valuedouble);
                }
                // scale
                cJSON *scale = cJSON_GetObjectItemCaseSensitive(node_json, "scale");
                if (cJSON_IsArray(scale) && cJSON_GetArraySize(scale) == 3)
                {
                    result.scale[0] = static_cast<float>(cJSON_GetArrayItem(scale, 0)->valuedouble);
                    result.scale[1] = static_cast<float>(cJSON_GetArrayItem(scale, 1)->valuedouble);
                    result.scale[2] = static_cast<float>(cJSON_GetArrayItem(scale, 2)->valuedouble);
                }
                // matrix
                cJSON *matrix_json = cJSON_GetObjectItemCaseSensitive(node_json, "matrix");
                if (cJSON_IsArray(matrix_json) && cJSON_GetArraySize(matrix_json) == 16)
                {
                    result.has_matrix = true;

                    for (std::size_t i = 0; i < 16; ++i)
                    {
                        result.matrix[i] = static_cast<float>(cJSON_GetArrayItem(matrix_json, i)->valuedouble);
                    }
                }

                nodes_.push_back(std::move(result));
            }
            return true;
        }

        bool ParseScenes(cJSON *root)
        {
            cJSON *scene = cJSON_GetObjectItemCaseSensitive(root, "scene");
            if (cJSON_IsNumber(scene))
            {
                default_scene_ = scene->valueint;
            }

            cJSON *scenes = cJSON_GetObjectItemCaseSensitive(root, "scenes");
            if (cJSON_IsArray(scenes))
            {
                scenes_.clear();
                scenes_.reserve(cJSON_GetArraySize(scenes));

                scene = nullptr;
                cJSON_ArrayForEach(scene, scenes)
                {
                    Scene result{};

                    cJSON *name = cJSON_GetObjectItemCaseSensitive(scene, "name");
                    if (cJSON_IsString(name))
                    {
                        result.name = name->valuestring;
                    }

                    cJSON *nodes = cJSON_GetObjectItemCaseSensitive(scene, "nodes");
                    if (cJSON_IsArray(nodes))
                    {
                        cJSON *node = nullptr;
                        cJSON_ArrayForEach(node, nodes)
                        {
                            if (cJSON_IsNumber(node))
                            {
                                result.nodes.push_back(node->valueint);
                            }
                        }
                    }
                    scenes_.push_back(std::move(result));
                }
            }
            return true;
        }

        void CopyVerticesFromBin(Primitive &primitive)
        {
            if (primitive.position_accessor < 0)
            {
                std::print("Position accessor is required by gltf spec.\n");
                return;
            }

            const Accessor &pos_accessor = accessors_[primitive.position_accessor];
            const std::size_t vertex_count = pos_accessor.count;

            // clean up vertices
            primitive.vertices.clear();
            primitive.vertices.resize(vertex_count);

            auto read_attribute_floats = [&](int32_t accessor_idx, size_t float_count, auto get_target_ptr)
            {
                if (accessor_idx < 0)
                {
                    return;
                }

                const Accessor &acc = accessors_[accessor_idx];
                const BufferView &view = buffer_views_[acc.buffer_view];

                const std::byte *src_bytes = binary_data_.data() + view.byte_offset + acc.byte_offset;

                const std::size_t element_size = float_count * sizeof(float);
                const std::size_t stride = (view.byte_stride > 0) ? view.byte_stride : element_size;

                if (stride == element_size)
                {
                    // fast path: contiguous data
                    for (std::size_t i = 0; i < vertex_count; ++i)
                    {
                        std::memcpy(get_target_ptr(primitive.vertices[i]), src_bytes + (i * element_size), element_size);
                    }
                }
                else
                {
                    // slow path: strided interleaved data
                    for (std::size_t i = 0; i < vertex_count; ++i)
                    {
                        std::memcpy(get_target_ptr(primitive.vertices[i]), src_bytes + (i * stride), element_size);
                    }
                }
            };

            // decode each attribute across all vertices at once
            // clang-format off
            read_attribute_floats(primitive.position_accessor, 3, [](Vertex &v) { return v.position; });
            read_attribute_floats(primitive.normal_accessor, 3, [](Vertex &v) { return v.normal; });
            read_attribute_floats(primitive.tangent_accessor, 4, [](Vertex &v) { return v.tangent; });
            read_attribute_floats(primitive.texcoord0_accessor, 2, [](Vertex &v) { return v.uv0; });
            read_attribute_floats(primitive.texcoord1_accessor, 2, [](Vertex &v) { return v.uv1; });
            read_attribute_floats(primitive.color0_accessor, 4, [](Vertex &v) { return v.color0; });
            read_attribute_floats(primitive.joints0_accessor, 4, [](Vertex &v) { return v.joints0; });
            read_attribute_floats(primitive.weights0_accessor, 4, [](Vertex &v) { return v.weights0; });
            // clang-format on
        }

        void CopyIndicesFromBin(Primitive &primitive)
        {
            if (primitive.index_accessor < 0)
            {
                return;
            }

            const Accessor &acc = accessors_[primitive.index_accessor];
            // Safety check on buffer_view
            if (acc.buffer_view < 0)
            {
                return;
            }

            const BufferView &view = buffer_views_[acc.buffer_view];

            primitive.indices.clear();
            primitive.indices.resize(acc.count);

            const std::byte *src_bytes = binary_data_.data() + view.byte_offset + acc.byte_offset;
            const std::size_t stride = view.byte_stride;

            switch (acc.component_type)
            {
            case 5121: // UNSIGNED_BYTE (uint8_t)
            {
                if (stride == 0 || stride == sizeof(std::uint8_t))
                {
                    for (std::size_t i = 0; i < acc.count; ++i)
                    {
                        primitive.indices[i] = static_cast<std::uint32_t>(reinterpret_cast<const std::uint8_t *>(src_bytes)[i]);
                    }
                }
                else
                {
                    for (std::size_t i = 0; i < acc.count; ++i)
                    {
                        const auto *ptr = reinterpret_cast<const std::uint8_t *>(src_bytes + (i * stride));
                        primitive.indices[i] = static_cast<std::uint32_t>(*ptr);
                    }
                }
                break;
            }
            case 5123: // UNSIGNED_SHORT (uint16_t)
            {
                if (stride == 0 || stride == sizeof(std::uint16_t))
                {
                    const auto *src16 = reinterpret_cast<const std::uint16_t *>(src_bytes);
                    for (std::size_t i = 0; i < acc.count; ++i)
                    {
                        primitive.indices[i] = static_cast<std::uint32_t>(src16[i]);
                    }
                }
                else
                {
                    for (std::size_t i = 0; i < acc.count; ++i)
                    {
                        const auto *ptr = reinterpret_cast<const std::uint16_t *>(src_bytes + (i * stride));
                        primitive.indices[i] = static_cast<std::uint32_t>(*ptr);
                    }
                }
                break;
            }
            case 5125: // UNSIGNED_INT (uint32_t)
            {
                if (stride == 0 || stride == sizeof(std::uint32_t))
                {
                    // FASTEST PATH: Directly copy contiguous 32-bit uint block
                    std::memcpy(primitive.indices.data(), src_bytes, acc.count * sizeof(std::uint32_t));
                }
                else
                {
                    for (std::size_t i = 0; i < acc.count; ++i)
                    {
                        std::memcpy(&primitive.indices[i], src_bytes + (i * stride), sizeof(std::uint32_t));
                    }
                }
                break;
            }
            default:
            {
                std::print("Unsupported index component type: {}\n", acc.component_type);
                assert(false && "Unsupported index component type");
                break;
            }
            }

            // Assertion only applies if rendering mode is TRIANGLES
            if (primitive.mode == PrimitiveRenderMode::kTriangles)
            {
                assert(primitive.indices.size() % 3 == 0);
            }
        }

        /// @brief Reads vertices from binary data and copies them over to library's local data structures.
        /// @param primitive One sub-mesh.
        void PopulatePrimitiveData(Primitive &primitive)
        {
            CopyVerticesFromBin(primitive);
            CopyIndicesFromBin(primitive);
        }

        /// @brief Using the buffer view index read the region of bytes where our image data is stored.
        /// @param image Struct containing buffer view index.
        void CopyImageData(Image &image)
        {
            // skip if image is sourced externally (URI) or has no buffer view assigned
            if (image.buffer_view < 0)
            {
                return;
            }

            const std::size_t view_idx = static_cast<std::size_t>(image.buffer_view);
            assert(view_idx < buffer_views_.size() && "Invalid bufferView index for image");

            const BufferView &view = buffer_views_[view_idx];
            assert(view.byte_offset + view.byte_length <= binary_data_.size() && "BufferView out of binary bounds");

            // copy raw compressed image bytes from GLB binary chunk
            image.data.resize(view.byte_length);
            std::memcpy(image.data.data(), binary_data_.data() + view.byte_offset, view.byte_length);
        }

        void TraverseNode(int index, const Matrix4x4 &parent_transform)
        {
            std::print("Visiting node {}\n", index);

            const Node &node = nodes_[index];
            const Matrix4x4 local_transform = node.has_matrix ? node.matrix : ComposeTranslationRotationScale(node.translation, node.rotation, node.scale);
            const Matrix4x4 world_transform = Multiply(parent_transform, local_transform);

            if (node.mesh >= 0)
            {
                MeshInstance instance{};
                instance.mesh = node.mesh;
                instance.world_transform = world_transform;

                mesh_instances_.push_back(std::move(instance));
            }

            for (int child : node.children)
            {
                TraverseNode(child, world_transform);
            }
        }

    public:
        Header header_{};
        std::vector<std::byte> binary_data_;
        std::vector<BufferView> buffer_views_;
        std::vector<Accessor> accessors_;

        std::vector<Mesh> meshes_;
        std::vector<MeshInstance> mesh_instances_;
        std::vector<Node> nodes_;
        std::vector<Scene> scenes_;
        int default_scene_{-1};

        std::vector<Material> materials_;
        std::vector<Texture> textures_;
        std::vector<Image> images_;
        std::vector<Sampler> samplers_;
    };
}