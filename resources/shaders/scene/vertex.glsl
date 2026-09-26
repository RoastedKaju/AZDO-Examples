#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

layout (location = 0) out vec2 vTexCoord;

layout (std140, binding = 0)
uniform PerFrameData
{
    mat4 uModelViewProj;
    uvec2 uBaseColorHandle;
};

void main()
{
    vTexCoord = aTexCoord;
    gl_Position = uModelViewProj * vec4(aPos, 1.0);
}