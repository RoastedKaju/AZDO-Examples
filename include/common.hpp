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
	std::string name;
	bool isSRGB = false;
};

struct Extent2D
{
	int width;
	int height;
};

struct GlTFModelData
{
	struct Vertex
	{
		glm::vec3 position{ 0.0f };
		glm::vec3 normal{ 0.0f };
		glm::vec2 texCoord{ 0.0f };
		glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
	};

	struct Material
	{
		std::string name;
		glm::vec4 baseColorFactor{ 1.0f };
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		int baseColorTextureIdx = -1;
		int normalTextureIdx = -1;
		int metallicRoughnessTextureIdx = -1;
		int metallicRoughnessImageIdx = -1;
		int emissiveTextureIdx = -1;
	};

	struct Primitive
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		int materialIndex = -1;
	};

	// Collection of primitives
	struct Mesh
	{
		std::string name;
		std::vector<Primitive> primitives;
	};

	struct Node
	{
		std::string name;
		int meshIndex = -1;
		glm::mat4 localTransform{ 1.0f };
		std::vector<int> childrenIndices;
	};

	std::string filePath;
	std::vector<ImageData> images;
	std::vector<Material> materials;
	std::vector<Mesh> meshes;
	std::vector<Node> nodes;
	std::vector<int> rootNodes;
};