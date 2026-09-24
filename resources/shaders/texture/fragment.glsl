#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;

layout (location = 0) out vec4 outColor;

layout (std430, binding = 0)
readonly buffer ObjectData
{
	uvec2 baseColorHandle;
} objectData;

void main()
{
	sampler2D baseColorSampler = sampler2D(objectData.baseColorHandle);
	outColor = texture(baseColorSampler, vTexCoord);
}