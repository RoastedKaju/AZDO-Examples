#pragma once

#include <fstream>
#include <filesystem>
#include <string>

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
}