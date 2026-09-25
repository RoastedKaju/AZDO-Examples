#pragma once

#include <stb_image.h>
#include <common.hpp>

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

	static inline bool ReadImageFile(const char* inPath, ImageData& outImageData, const GLuint inDesiredChannels = 0, bool doFlipVertically = false)
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
			loadedData = stbi_loadf(inPath, &width, &height, &channels, inDesiredChannels);
			type = ImageData::Type::eFloat32;
		}
		else if (stbi_is_16_bit(inPath))
		{
			loadedData = stbi_load_16(inPath, &width, &height, &channels, inDesiredChannels);
			type = ImageData::Type::eUInt16;
		}
		else
		{
			loadedData = stbi_load(inPath, &width, &height, &channels, inDesiredChannels);
			type = ImageData::Type::eUInt8;
		}

		if (!loadedData)
		{
			std::cerr << "Failed to load image: " << inPath << std::endl;
			std::cerr << "Reason: " << stbi_failure_reason() << std::endl;
			stbi_image_free(loadedData);
			return false;
		}

		const int actualChannels = inDesiredChannels == 0 ? channels : static_cast<int>(inDesiredChannels);

		if (width <= 0 || height <= 0 || channels < 1 || channels > 4)
		{
			std::cerr << "Invalid components in Image: " << inPath << std::endl;
			stbi_image_free(loadedData);
			return false;
		}

		const size_t bytesPerComponent = type == ImageData::Type::eUInt8 ? 1 : type == ImageData::Type::eUInt16 ? 2 : 4;
		const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
		const size_t byteCount = pixelCount * static_cast<size_t>(actualChannels) * bytesPerComponent;

		ImageData result
		{
			.type = type,
			.width = width,
			.height = height,
			.channels = actualChannels
		};
		result.pixels.resize(byteCount);

		std::memcpy(result.pixels.data(), loadedData, byteCount);

		stbi_image_free(loadedData);

		outImageData = std::move(result);
		return true;
	}

	static inline bool ReadImageMemory(std::span<const std::uint8_t> encodedBytes, ImageData& outImageData, const GLuint inDesiredChannels = 0, bool doFlipVertically = false)
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

		const int actualChannels = inDesiredChannels == 0 ? channels : static_cast<int>(inDesiredChannels);

		if (width <= 0 || height <= 0 || channels < 1 || channels > 4)
		{
			stbi_image_free(loadedData);
			return false;
		}

		const size_t bytesPerComponent = type == ImageData::Type::eUInt8 ? 1 : type == ImageData::Type::eUInt16 ? 2 : 4;
		const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(actualChannels) * bytesPerComponent;

		ImageData result
		{
			.type = type,
			.width = width,
			.height = height,
			.channels = actualChannels
		};
		result.pixels.resize(byteCount);
		std::memcpy(result.pixels.data(), loadedData, byteCount);

		stbi_image_free(loadedData);
		outImageData = std::move(result);
		return true;
	}
}