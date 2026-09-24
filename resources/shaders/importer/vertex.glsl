#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

layout (location = 0) out vec2 vTexCoord;

layout (std140, binding = 0)
readonly uniform FrameData
{
	mat4 model;
	mat4 view;
	mat4 proj;
	uvec2 baseColorHandle;
};

void main()
{
	vTexCoord = aTexCoord;
	gl_Position = proj * view * model * vec4(aPos, 1.0);
}