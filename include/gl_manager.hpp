#pragma once

#include <glad/gl.h>
#include <glfw/glfw3.h>
#include <common.hpp>

namespace GlDevice
{
	static inline GLuint UploadImageToGPU(const ImageData& inImage)
	{
		GLenum internalFormat = 0;
		GLenum format = 0;

		// select formats
		switch (inImage.channels)
		{
		case 1:
			format = GL_RED;
			switch (inImage.type)
			{
			case ImageData::Type::eUInt8:   internalFormat = GL_R8; break;
			case ImageData::Type::eUInt16:  internalFormat = GL_R16; break;
			case ImageData::Type::eFloat32: internalFormat = GL_R32F; break;
			}
			break;

		case 2:
			format = GL_RG;
			switch (inImage.type)
			{
			case ImageData::Type::eUInt8:   internalFormat = GL_RG8; break;
			case ImageData::Type::eUInt16:  internalFormat = GL_RG16; break;
			case ImageData::Type::eFloat32: internalFormat = GL_RG32F; break;
			}
			break;

		case 3:
			format = GL_RGB;
			switch (inImage.type)
			{
			case ImageData::Type::eUInt8:   internalFormat = GL_RGB8; break;
			case ImageData::Type::eUInt16:  internalFormat = GL_RGB16; break;
			case ImageData::Type::eFloat32: internalFormat = GL_RGB32F; break;
			}
			break;

		case 4:
			format = GL_RGBA;
			switch (inImage.type)
			{
			case ImageData::Type::eUInt8:   internalFormat = GL_RGBA8; break;
			case ImageData::Type::eUInt16:  internalFormat = GL_RGBA16; break;
			case ImageData::Type::eFloat32: internalFormat = GL_RGBA32F; break;
			}
			break;

		default:
			std::cerr << "Unsupported channel count: " << inImage.channels << std::endl;
			return 0;
		}

		// select data type
		GLenum dataType = GL_UNSIGNED_BYTE;
		switch (inImage.type)
		{
		case ImageData::Type::eUInt8:   dataType = GL_UNSIGNED_BYTE; break;
		case ImageData::Type::eUInt16:  dataType = GL_UNSIGNED_SHORT; break;
		case ImageData::Type::eFloat32: dataType = GL_FLOAT; break;
		}

		glPixelStorei(GL_UNPACK_ALIGNMENT, (inImage.channels == 1 || inImage.channels == 3) ? 1 : 4);

		GLuint textureID = 0;
		GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(inImage.width, inImage.height)))) + 1;

		glCreateTextures(GL_TEXTURE_2D, 1, &textureID);
		glTextureStorage2D(textureID, levels, internalFormat, inImage.width, inImage.height);
		glTextureSubImage2D(textureID, 0, 0, 0, inImage.width, inImage.height, format, dataType, inImage.pixels.data());

		glGenerateTextureMipmap(textureID);
		glTextureParameteri(textureID, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(textureID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(textureID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(textureID, GL_TEXTURE_WRAP_T, GL_REPEAT);

		// Reset pixel alignment state to default
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

		return textureID;
	}

	static inline GLuint64 GetTextureHandle(const GLuint inTextureID)
	{
		GLuint64 textureHandle = glGetTextureHandleARB(inTextureID);
		if (textureHandle == 0)
		{
			std::cerr << "Could not get texture handle, it is recommended to delete this texture.\n";
			return 0;
		}

		glMakeTextureHandleResidentARB(textureHandle);
		return textureHandle;
	}

	class DefaultTextureSet
	{
	public:
		void Initialize()
		{
			const uint8_t white[4] = { 255, 255, 255, 255 };
			const uint8_t flatNormal[4] = { 128, 128, 225, 225 };
			const uint8_t metallicRoughness[4] = { 0, 255, 0, 255 };
			const uint8_t black[4] = { 0, 0, 0, 255 };

			baseColorHandle = Create1x1Texture(white);
			normalMapHandle = Create1x1Texture(flatNormal);
			metallicRoughnessMapHandle = Create1x1Texture(metallicRoughness);
			occlusionHandle = Create1x1Texture(white);
			emissiveHandle = Create1x1Texture(black);
		}

		void Destroy()
		{
			for (GLuint64 handle : {baseColorHandle, normalMapHandle, metallicRoughnessMapHandle, occlusionHandle, emissiveHandle})
			{
				if (handle) { glMakeTextureHandleNonResidentARB(handle); }
			}
			for (GLuint tex : textures)
			{
				if (tex) { glDeleteTextures(1, &tex); }
			}
		}

		GLuint64 baseColorHandle{ 0 };
		GLuint64 normalMapHandle{ 0 };
		GLuint64 metallicRoughnessMapHandle{ 0 };
		GLuint64 occlusionHandle{ 0 };
		GLuint64 emissiveHandle{ 0 };

	private:
		GLuint64 Create1x1Texture(const uint8_t inPixels[4])
		{
			GLuint textureID = 0;
			glCreateTextures(GL_TEXTURE_2D, 1, &textureID);
			glTextureStorage2D(textureID, 1, GL_RGBA8, 1, 1);
			glTextureSubImage2D(textureID, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inPixels);

			glTextureParameteri(textureID, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(textureID, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

			GLuint64 handle = glGetTextureHandleARB(textureID);
			if (handle != 0)
			{
				glMakeTextureHandleResidentARB(handle);
				textures.push_back(textureID);
			}
			return handle;
		}

		std::vector<GLuint> textures;
	};
}