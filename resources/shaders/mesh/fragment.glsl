#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec3 vColor;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0)
uniform FrameData
{
	mat4 MVP;
	vec4 wireframeColor;
};

void main()
{
	if(wireframeColor.a > 0.0)
	{
		outColor = wireframeColor;
	}
	else
	{
		outColor = vec4(vColor, 1.0);
	}
}