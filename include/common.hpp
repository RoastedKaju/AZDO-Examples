#pragma once

#include <glfw/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <string>
#include <fstream>
#include <utility>
#include <algorithm>
#include <vector>
#include <memory>
#include <filesystem>
#include <span>

struct ImageData
{
	enum class Type
	{
		eUInt8,
		eUInt16,
		eFloat32
	} type;

	int width;
	int height;
	int channels; // 1 = gray, 2 = gray + alpha, 3 = RGB, 4 = RGBA
	std::vector<std::uint8_t> pixels;
};

struct Extent2D
{
	int width;
	int height;
};

struct ModelData
{

};