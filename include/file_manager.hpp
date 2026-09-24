#pragma once

#include <stb_image.h>
#include <tiny_gltf_v3.h>
#include <common.hpp>
#include <gl_manager.hpp>

namespace FileManager
{
	static inline std::string ReadTextFile(const std::filesystem::path& inPath)
	{
		std::ifstream file(inPath, std::ios::binary | std::ios::ate);
		if (!file)
		{
			return {};
		}

		const auto size = file.tellg();
		if (size < 0)
		{
			return {};
		}

		std::string contents(static_cast<std::size_t>(size), '\0');

		file.seekg(0);
		if (!file.read(contents.data(), size))
		{
			return {};
		}

		return contents;
	}

	static inline bool ReadImageFile(const char* inPath, ImageData& outImageData, bool doFlipVertically = false)
	{
		if (!std::filesystem::exists(inPath))
		{
			std::cerr << "Image file does not exist: " << inPath << std::endl;
			return false;
		}

		stbi_set_flip_vertically_on_load(doFlipVertically);

		int width = 0;
		int height = 0;
		int channels = 0;
		void* loadedData = nullptr;
		ImageData::Type type{};

		if (stbi_is_hdr(inPath))
		{
			loadedData = stbi_loadf(inPath, &width, &height, &channels, 0);
			type = ImageData::Type::eFloat32;
		}
		else if (stbi_is_16_bit(inPath))
		{
			loadedData = stbi_load_16(inPath, &width, &height, &channels, 0);
			type = ImageData::Type::eUInt16;
		}
		else
		{
			loadedData = stbi_load(inPath, &width, &height, &channels, 0);
			type = ImageData::Type::eUInt8;
		}

		if (!loadedData)
		{
			std::cerr << "Failed to load image: " << inPath << std::endl;
			std::cerr << "Reason: " << stbi_failure_reason() << std::endl;
			stbi_image_free(loadedData);
			return false;
		}

		if (width <= 0 || height <= 0 || channels < 1 || channels > 4)
		{
			std::cerr << "Invalid components in Image: " << inPath << std::endl;
			stbi_image_free(loadedData);
			return false;
		}

		const size_t bytesPerComponent = type == ImageData::Type::eUInt8 ? 1 : type == ImageData::Type::eUInt16 ? 2 : 4;
		const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
		const size_t byteCount = pixelCount * static_cast<size_t>(channels) * bytesPerComponent;

		ImageData result
		{
			.type = type,
			.width = width,
			.height = height,
			.channels = channels
		};
		result.pixels.resize(byteCount);

		std::memcpy(result.pixels.data(), loadedData, byteCount);

		stbi_image_free(loadedData);

		outImageData = std::move(result);
		return true;
	}

	static inline bool ReadImageMemory(std::span<const std::uint8_t> encodedBytes, ImageData& outImageData, const int inDesiredChannels = 4, bool doFlipVertically = false)
	{
		if (encodedBytes.empty() || encodedBytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			std::cerr << "Invalid encoded image buffer\n";
			return false;
		}

		stbi_set_flip_vertically_on_load(doFlipVertically);

		const stbi_uc* source = encodedBytes.data();
		const int sourceSize = static_cast<int>(encodedBytes.size());

		int width = 0;
		int height = 0;
		int channels = inDesiredChannels;
		void* loadedData = nullptr;
		ImageData::Type type{};

		if (stbi_is_hdr_from_memory(source, sourceSize))
		{
			loadedData = stbi_loadf_from_memory(source, sourceSize, &width, &height, &channels, inDesiredChannels);
			type = ImageData::Type::eFloat32;
		}
		else if (stbi_is_16_bit_from_memory(source, sourceSize))
		{
			loadedData = stbi_load_16_from_memory(source, sourceSize, &width, &height, &channels, inDesiredChannels);
			type = ImageData::Type::eUInt16;
		}
		else
		{
			loadedData = stbi_load_from_memory(source, sourceSize, &width, &height, &channels, inDesiredChannels);
			type = ImageData::Type::eUInt8;
		}

		if (!loadedData)
		{
			std::cerr << "Failed to load image from memory: " << stbi_failure_reason() << '\n';
			return false;
		}

		if (width <= 0 || height <= 0 || channels < 1 || channels > 4)
		{
			stbi_image_free(loadedData);
			return false;
		}

		const size_t bytesPerComponent = type == ImageData::Type::eUInt8 ? 1 : type == ImageData::Type::eUInt16 ? 2 : 4;
		const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(inDesiredChannels) * bytesPerComponent;

		ImageData result
		{
			.type = type,
			.width = width,
			.height = height,
			.channels = inDesiredChannels
		};
		result.pixels.resize(byteCount);
		std::memcpy(result.pixels.data(), loadedData, byteCount);

		stbi_image_free(loadedData);
		outImageData = std::move(result);
		return true;
	}

	static inline const uint8_t* GetAccessorData(const tg3_model& inModel, int inIndex, size_t& outStride, size_t outCount)
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

	static inline bool LoadGlTF(const std::string& inPath)
	{
		if (!std::filesystem::exists(inPath))
		{
			std::cerr << "File does not exist: " << inPath << std::endl;
			return false;
		}

		std::cout << "Loading GlTF: " << inPath << std::endl;
		// Load and parse GlTF model
		tg3_model model;
		tg3_parse_options opts;
		tg3_error_stack errors;

		tg3_parse_options_init(&opts);
		tg3_error_stack_init(&errors);
		tg3_error_code parseResult = tg3_parse_file(&model, &errors, inPath.c_str(), inPath.size(), &opts);

		if (parseResult != TG3_OK)
		{
			std::cerr << "Error parsing glTF file, errors found: \n";
			for (int i = 0; i < errors.count; ++i)
			{
				std::cerr << errors.entries[i].message << std::endl;
			}
			tg3_error_stack_free(&errors);
			return false;
		}
		tg3_error_stack_free(&errors);

		std::filesystem::path imageDir = std::filesystem::path(inPath).parent_path();

		// Load images into RAM
		std::vector<ImageData> images(model.images_count);
		for (uint32_t i = 0; i < model.images_count; ++i)
		{
			const tg3_image& image = model.images[i];

			if (image.uri.data != nullptr && image.uri.len > 0)
			{
				std::filesystem::path imagePath = imageDir / model.images[i].uri.data;
				ReadImageFile(imagePath.string().c_str(), images[i], false);
			}
			else if (image.buffer_view >= 0)
			{
				// load embedded images in binary file
				const tg3_buffer_view& view = model.buffer_views[image.buffer_view];
				const tg3_buffer& buffer = model.buffers[view.buffer];
				const unsigned char* bytes = buffer.data.data + view.byte_offset;
				ReadImageMemory(std::span<const std::uint8_t>(bytes, static_cast<size_t>(view.byte_length)), images[i]);
			}
		}

		// Walk through every mesh inside GlTF model
		for (uint32_t i = 0; i < model.meshes_count; ++i)
		{
			const tg3_mesh& tgMesh = model.meshes[i];

			// loop over primitives
			for (uint32_t j = 0; j < tgMesh.primitives_count; ++j)
			{
				const tg3_primitive& tgPrimitive = tgMesh.primitives[j];

				int posAccessor = -1;
				int texCoordAccessor = -1;
				int normalAccessor = -1;
				int tangentAccessor = -1;
				for (uint32_t k = 0; k < tgPrimitive.attributes_count; ++k)
				{
					const tg3_str_int_pair& attribute = tgPrimitive.attributes[k];
					if (tg3_str_equals_cstr(attribute.key, "POSITION")) posAccessor = attribute.value;
					else if (tg3_str_equals_cstr(attribute.key, "TEXCOORD_0")) texCoordAccessor = attribute.value;
					else if (tg3_str_equals_cstr(attribute.key, "NORMAL")) normalAccessor = attribute.value;
					else if (tg3_str_equals_cstr(attribute.key, "TANGENT")) tangentAccessor = attribute.value;
				}

				// get accessor data
				if (posAccessor < 0)
				{
					std::cerr << "Primitive missing 'POSITION' attribute." << std::endl;
					continue;
				}
				// position
				size_t posStride = 0, posCount = 0;
				const uint8_t* posData = GetAccessorData(model, posAccessor, posStride, posCount);
				if (!posData)
				{
					std::cerr << "'POSITION' accessor has no backing buffer view, skipping primitive.\n";
					continue;
				}
				// texture coordinates
				size_t uvStride = 0, uvCount = 0;
				const uint8_t* uvData = nullptr;
				if (texCoordAccessor >= 0)
				{
					uvData = GetAccessorData(model, texCoordAccessor, uvStride, uvCount);
				}
				// normal
				size_t normalStride = 0, normalCount = 0;
				const uint8_t* normalData = nullptr;
				if (normalAccessor >= 0)
				{
					normalData = GetAccessorData(model, normalAccessor, normalStride, normalCount);
				}
				// tangent
				size_t tangentStride = 0, tangentCount = 0;
				const uint8_t* tangentData = nullptr;
				if (tangentAccessor >= 0)
				{
					tangentData = GetAccessorData(model, tangentAccessor, tangentStride, tangentCount);
				}

				// Arrange vertex data

				// fill up indices


			}
		}

		tg3_model_free(&model);

		return true;
	}
}