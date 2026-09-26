#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;

layout (location = 0) out vec4 outColor;

layout (std140, binding = 0)
uniform PerFrameData
{
    mat4 uModelViewProj;
    uvec2 uBaseColorHandle;
};

void main()
{
    sampler2D baseColSampler = sampler2D(uBaseColorHandle);
    outColor = texture(baseColSampler, vTexCoord);
}