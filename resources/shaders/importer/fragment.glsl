#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;

layout (location = 0) out vec4 outColor;

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
	sampler2D baseColorSampler = sampler2D(baseColorHandle);
	outColor = texture(baseColorSampler, vTexCoord);

	//outColor = vec4(1.0, 0.0, 0.0, 1.0);
}