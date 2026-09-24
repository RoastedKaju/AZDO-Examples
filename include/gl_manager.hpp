#pragma once

#include <glad/gl.h>
#include <glfw/glfw3.h>
#include <common.hpp>

namespace GlDevice
{
	static inline GLuint UploadImageToGPU(const ImageData& inImage)
	{
		GLuint textureID;
		GLsizei levels = static_cast<GLsizei>(std::floor(std::log2(std::max(inImage.width, inImage.height)))) + 1;

		glCreateTextures(GL_TEXTURE_2D, 1, &textureID);
		glTextureStorage2D(textureID, levels, GL_RGBA8, inImage.width, inImage.height);
		glTextureSubImage2D(textureID, 0, 0, 0, inImage.width, inImage.height, GL_RGBA, GL_UNSIGNED_BYTE, inImage.pixels.data());

		glGenerateTextureMipmap(textureID);
		glTextureParameteri(textureID, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(textureID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(textureID, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(textureID, GL_TEXTURE_WRAP_T, GL_REPEAT);

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