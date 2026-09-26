#pragma once

#include <print>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cstddef>
#include <cassert>
#include <cJSON.h>

namespace Okara::GlTF
{
    static constexpr std::uint32_t GLB_MAGIC = 0x46546C67;
    static constexpr std::uint32_t GLB_VERSION = 2;
    static constexpr std::uint32_t JSON_CHUNK = 0x4E4F534A;
    static constexpr std::uint32_t BIN_CHUNK = 0x004E4942;

    struct Header
    {
        uint32_t magic{};
        uint32_t version{};
        uint32_t length{};
    };

    struct ChunkHeader
    {
        uint32_t length{};
        uint32_t type{};
    };

    // TODO: these should be std::size_t
    struct BufferView
    {
        int buffer{};
        int byte_offset{};
        int byte_length{};
        int byte_stride{};
    };
    // TODO: these should be std::size_t
    struct Accessor
    {
        int buffer_view{};
        int byte_offset{};
        int component_type{};
        int count{};
        std::string type{};
    };

    enum class PrimitiveMode
    {
        points = 0,
        lines = 1,
        line_loop = 2,
        line_strip = 3,
        triangles = 4,
        triangle_strip = 5,
        triangle_fan = 6
    };

    struct Vec4
    {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    struct Vec3
    {
        float x{};
        float y{};
        float z{};
    };

    struct Vec2
    {
        float x{};
        float y{};
    };

    struct Vertex
    {
        Vec4 tangent{};
        Vec3 position{};
        Vec3 normal{};
        Vec2 uv{};
    };

    struct Material
    {
        std::string name{};
        Vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic_factor{1.0f};
        float roughness_factor{1.0f};
        int base_color_texture{-1};
    };

    struct Image
    {
        std::string name{};
        std::string uri{};
        std::string mime_type{};
        std::vector<std::byte> data{};
        int buffer_view{-1};
    };

    struct Texture
    {
        int source{-1};
    };

    struct Primitive
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::size_t material{};
        PrimitiveMode mode{PrimitiveMode::triangles};
    };

    class Loader
    {
    public:
        bool load(const std::string &path)
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

            printHeaderData();

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
            printChunkHeaderData(&chunk_header);

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
            if (!iterateOverBuffers(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // buffer views
            if (!iterateOverBufferViews(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // accessors know how to interpert the data where buffer views point
            if (!iterateOverAccessors(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // inspect meshes
            cJSON *meshes = cJSON_GetObjectItemCaseSensitive(root, "meshes");
            if (!cJSON_IsArray(meshes))
            {
                std::print("GLB does not contain a valid meshes array.\n");
                cJSON_Delete(root);
                return false;
            }

            std::print("Meshes: {}\n", cJSON_GetArraySize(meshes));

            cJSON *mesh = nullptr;
            cJSON_ArrayForEach(mesh, meshes)
            {
                cJSON *primitives = cJSON_GetObjectItemCaseSensitive(mesh, "primitives");
                if (!cJSON_IsArray(primitives))
                {
                    std::print("Mesh does not contain a valid primitives array.\n");
                    cJSON_Delete(root);
                    return false;
                }

                std::print("Primitives: {}\n", cJSON_GetArraySize(primitives));

                cJSON *primitive = nullptr;
                cJSON_ArrayForEach(primitive, primitives)
                {
                    cJSON *attributes = cJSON_GetObjectItemCaseSensitive(primitive, "attributes");
                    // attributes value in glTF is an object not an array
                    if (!cJSON_IsObject(attributes))
                    {
                        std::print("Primitive does not contain valid attributes.\n");
                        cJSON_Delete(root);
                        return false;
                    }

                    cJSON *attribute = nullptr;
                    cJSON_ArrayForEach(attribute, attributes)
                    {
                        std::print("Attribute: {} -> Accessor {}\n", attribute->string, attribute->valueint);
                        // store position accessor index
                        if (std::strcmp(attribute->string, "POSITION") == 0)
                        {
                            position_accessor_idx_ = static_cast<size_t>(attribute->valueint);
                        }
                        // store normal accessor index
                        else if (std::strcmp(attribute->string, "NORMAL") == 0)
                        {
                            normal_accessor_idx_ = static_cast<size_t>(attribute->valueint);
                        }
                        // texture coordinates accessor index
                        else if (std::strcmp(attribute->string, "TEXCOORD_0") == 0)
                        {
                            texcoord_accessor_idx_ = static_cast<size_t>(attribute->valueint);
                        }
                        // tangent accessor index
                        else if (std::strcmp(attribute->string, "TANGENT") == 0)
                        {
                            tangent_accessor_idx_ = static_cast<size_t>(attribute->valueint);
                        }
                    }

                    cJSON *indices = cJSON_GetObjectItemCaseSensitive(primitive, "indices");
                    if (cJSON_IsNumber(indices))
                    {
                        std::print("Indices accessor: {}\n", indices->valueint);
                        index_accessor_idx_ = static_cast<size_t>(indices->valueint);
                    }

                    cJSON *mode = cJSON_GetObjectItemCaseSensitive(primitive, "mode");
                    if (cJSON_IsNumber(mode))
                    {
                        std::print("Primitive mode: {}\n", mode->valueint);
                        primitive_mode_ = static_cast<PrimitiveMode>(mode->valueint);
                    }
                    // this connects a primitive to our materials array
                    cJSON *material = cJSON_GetObjectItemCaseSensitive(primitive, "material");
                    if (cJSON_IsNumber(material))
                    {
                        std::print("Primitive material index: {}\n", material->valueint);
                        material_idx_ = static_cast<size_t>(material->valueint);
                    }
                }
            }

            // parse materials
            if (!iterateOverMaterials(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // parse textures
            if (!iterateOverTextures(root))
            {
                cJSON_Delete(root);
                return false;
            }

            // inspect images
            if (!iterateOverImages(root))
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
            primitive_ = buildMesh();

            // extract images data
            for (Image &image : images_)
            {
                if (image.buffer_view >= 0)
                {
                    extractImageData(image);
                }
            }

            const Material &material = materials_[primitive_.material];
            if (material.base_color_texture >= 0)
            {
                const Texture &texture = textures_[static_cast<std::size_t>(material.base_color_texture)];
                const Image &image = images_[static_cast<std::size_t>(texture.source)];

                std::print("Mesh material: {}\n", material.name);
                std::print("Texture image MIME: {}\n", image.mime_type);
                std::print("Texture image size: {} bytes\n", image.data.size());
            }

            // release the temperory data
            binary_data_.clear();
            binary_data_.shrink_to_fit();

            return true;
        }

    private:
        void printHeaderData()
        {
            std::print("Magic: 0x{:08X}, Version: {}, Length: {}\n", header_.magic, header_.version, header_.length);
        }

        void printChunkHeaderData(ChunkHeader *chunk_header)
        {
            std::print("Chunk Header Type: {:08X}, Length: {}\n", chunk_header->type, chunk_header->length);
        }

        bool iterateOverBuffers(cJSON *root)
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

        bool iterateOverBufferViews(cJSON *root)
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

                buffer_views_.push_back({buffer->valueint, offset, byte_length->valueint, stride});
            }

            return true;
        }

        bool iterateOverAccessors(cJSON *root)
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

                accessors_.push_back({view, offset, component_type->valueint, count->valueint, type->valuestring});
            }

            return true;
        }

        bool iterateOverMaterials(cJSON *root)
        {
            cJSON *materials = cJSON_GetObjectItemCaseSensitive(root, "materials");
            if (!cJSON_IsArray(materials))
            {
                std::print("GLB does not contain any valid materials array.\n");
                return false;
            }

            std::print("Materials: {}\n", cJSON_GetArraySize(materials));
            materials_.reserve(cJSON_GetArraySize(materials)); // reserve size for our material array

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
                // pbr, roughness, metallic, base color texture
                cJSON *pbr = cJSON_GetObjectItemCaseSensitive(material, "pbrMetallicRoughness");
                if (cJSON_IsObject(pbr))
                {
                    cJSON *metallic = cJSON_GetObjectItemCaseSensitive(pbr, "metallicFactor");
                    if (cJSON_IsNumber(metallic))
                    {
                        std::print("Metallic: {}\n", metallic->valuedouble);
                        result.metallic_factor = static_cast<float>(metallic->valuedouble);
                    }
                    cJSON *roughness = cJSON_GetObjectItemCaseSensitive(pbr, "roughnessFactor");
                    if (cJSON_IsNumber(roughness))
                    {
                        std::print("Roughness: {}\n", roughness->valuedouble);
                        result.roughness_factor = static_cast<float>(roughness->valuedouble);
                    }
                    cJSON *base_color_texture = cJSON_GetObjectItemCaseSensitive(pbr, "baseColorTexture");
                    if (cJSON_IsObject(base_color_texture))
                    {
                        cJSON *index = cJSON_GetObjectItemCaseSensitive(base_color_texture, "index");
                        if (cJSON_IsNumber(index))
                        {
                            std::print("Base color texture index: {}\n", index->valueint);
                            result.base_color_texture = index->valueint;
                        }
                    }
                    // TODO: get base color factor
                }

                materials_.push_back(std::move(result));
            }

            return true;
        }

        bool iterateOverTextures(cJSON *root)
        {
            cJSON *textures = cJSON_GetObjectItemCaseSensitive(root, "textures");
            if (!cJSON_IsArray(textures))
            {
                std::print("GLB does not contain a valid textures array.\n");
                return false;
            }

            std::print("Textures: {}\n", cJSON_GetArraySize(textures));
            textures_.reserve(cJSON_GetArraySize(textures));

            cJSON *texture = nullptr;
            cJSON_ArrayForEach(texture, textures)
            {
                Texture result{};

                cJSON *source = cJSON_GetObjectItemCaseSensitive(texture, "source");
                if (cJSON_IsNumber(source))
                {
                    std::print("Texture source image index: {}\n", source->valueint);
                    result.source = source->valueint;
                }

                textures_.push_back(std::move(result));
            }

            return true;
        }

        bool iterateOverImages(cJSON *root)
        {
            cJSON *images = cJSON_GetObjectItemCaseSensitive(root, "images");
            if (!cJSON_IsArray(images))
            {
                std::print("GLB does not contain a valid images array.\n");
                return false;
            }

            std::print("Images: {}\n", cJSON_GetArraySize(images));
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

        size_t componentTypeSize(uint32_t component_type)
        {
            switch (component_type)
            {
            case 5120: // signed byte
                return sizeof(uint8_t);
            case 5121: // unsigned byte
                return sizeof(uint8_t);
            case 5122: // signed short
                return sizeof(uint16_t);
            case 5123: // unsigned short
                return sizeof(uint16_t);
            case 5125: // unsigned int
                return sizeof(uint32_t);
            case 5126: // float
                return sizeof(float);
            default:
                return 0;
            }
        }

        size_t componentCount(const std::string &type)
        {
            if (type == "SCALAR")
                return 1;
            if (type == "VEC2")
                return 2;
            if (type == "VEC3")
                return 3;
            if (type == "VEC4")
                return 4;

            return 0;
        }

        size_t calculateOffset(const Accessor &accessor, size_t index)
        {
            assert(index < accessor.count);
            const std::size_t component_size = componentTypeSize(accessor.component_type);
            const std::size_t component_count = componentCount(accessor.type);
            const std::size_t element_size = component_size * component_count;
            const BufferView &view = buffer_views_[accessor.buffer_view];
            const size_t view_offset = view.byte_offset + accessor.byte_offset;

            const size_t stride = view.byte_stride != 0 ? view.byte_stride : element_size;
            return view_offset + index * stride;
        }

        Vec4 readVec4(const Accessor &accessor, size_t index)
        {
            Vec4 result{};
            const size_t offset = calculateOffset(accessor, index);
            std::memcpy(&result.x, binary_data_.data() + offset + 0, sizeof(float));
            std::memcpy(&result.y, binary_data_.data() + offset + 4, sizeof(float));
            std::memcpy(&result.z, binary_data_.data() + offset + 8, sizeof(float));
            std::memcpy(&result.w, binary_data_.data() + offset + 12, sizeof(float));

            return result;
        }

        Vec3 readVec3(const Accessor &accessor, size_t index)
        {

            Vec3 result{};
            const size_t offset = calculateOffset(accessor, index);
            std::memcpy(&result.x, binary_data_.data() + offset + 0, sizeof(float));
            std::memcpy(&result.y, binary_data_.data() + offset + 4, sizeof(float));
            std::memcpy(&result.z, binary_data_.data() + offset + 8, sizeof(float));

            return result;
        }

        Vec2 readVec2(const Accessor &accessor, size_t index)
        {
            Vec2 result{};
            const size_t offset = calculateOffset(accessor, index);
            std::memcpy(&result.x, binary_data_.data() + offset + 0, sizeof(float));
            std::memcpy(&result.y, binary_data_.data() + offset + 4, sizeof(float));

            return result;
        }

        Vec3 readPosition(std::size_t index)
        {
            return readVec3(accessors_[position_accessor_idx_], index);
        }

        Vec3 readNormal(std::size_t index)
        {
            return readVec3(accessors_[normal_accessor_idx_], index);
        }

        Vec2 readTexcoord(std::size_t index)
        {
            return readVec2(accessors_[texcoord_accessor_idx_], index);
        }

        Vec4 readTangent(std::size_t index)
        {
            return readVec4(accessors_[tangent_accessor_idx_], index);
        }

        uint32_t readIndex(std::size_t index)
        {
            const Accessor &accessor = accessors_[index_accessor_idx_];
            const size_t offset = calculateOffset(accessor, index);

            switch (accessor.component_type)
            {
            case 5121: // unsigned byte
            {
                uint8_t v{};
                std::memcpy(&v, binary_data_.data() + offset, sizeof(uint8_t));
                return v;
            }
            case 5123: // unsigned short
            {
                uint16_t v{};
                std::memcpy(&v, binary_data_.data() + offset, sizeof(uint16_t));
                return v;
            }
            case 5125: // unsigned int
            {
                uint32_t v{};
                std::memcpy(&v, binary_data_.data() + offset, sizeof(uint32_t));
                return v;
            }
            default:
                assert(false && "Unsupported index component type");
                return 0;
            }

            return 0;
        }

        Vertex readVertex(std::size_t index)
        {
            Vertex result{};

            result.position = readPosition(index);
            result.normal = readNormal(index);
            result.uv = readTexcoord(index);
            result.tangent = readTangent(index);

            return result;
        }

        std::size_t triangleCount() const
        {
            const Accessor &index_accessor = accessors_[index_accessor_idx_];
            assert(index_accessor.count % 3 == 0);
            return index_accessor.count / 3;
        }

        Primitive buildMesh()
        {
            Primitive mesh{};

            const Accessor &position_accessor = accessors_[position_accessor_idx_];
            mesh.vertices.reserve(position_accessor.count);
            for (size_t i = 0; i < position_accessor.count; ++i)
            {
                mesh.vertices.push_back(readVertex(i));
            }

            const Accessor &index_accessor = accessors_[index_accessor_idx_];
            mesh.indices.reserve(index_accessor.count);
            for (size_t i = 0; i < index_accessor.count; ++i)
            {
                mesh.indices.push_back(readIndex(i));
            }
            assert(mesh.indices.size() % 3 == 0);

            mesh.mode = primitive_mode_;

            mesh.material = material_idx_;

            return mesh;
        }

        void extractImageData(Image &image)
        {
            assert(image.buffer_view >= 0);

            const BufferView &view = buffer_views_[static_cast<std::size_t>(image.buffer_view)];
            assert(view.byte_offset + view.byte_length <= binary_data_.size());

            image.data.assign(binary_data_.begin() + view.byte_offset, binary_data_.begin() + view.byte_offset + view.byte_length);
        }

    public:
        Header header_{};
        std::vector<std::byte> binary_data_;
        std::vector<BufferView> buffer_views_;
        std::vector<Accessor> accessors_;

        size_t position_accessor_idx_;
        size_t normal_accessor_idx_;
        size_t index_accessor_idx_;
        size_t texcoord_accessor_idx_;
        size_t tangent_accessor_idx_;

        PrimitiveMode primitive_mode_;
        size_t material_idx_;

        std::vector<Material> materials_;
        std::vector<Texture> textures_;
        std::vector<Image> images_;

        Primitive primitive_;
    };
}