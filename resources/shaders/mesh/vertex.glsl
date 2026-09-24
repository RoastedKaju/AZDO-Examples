#version 460 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

layout (location = 0) out vec3 vColor;

layout (std140, binding = 0)
readonly uniform FrameData
{
	mat4 MVP;
	vec4 wireframeColor;
};

void main()
{
	vColor = aColor;
	gl_Position = MVP * vec4(aPos, 1.0);
}