#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in int vDrawID;

layout (location = 0) out vec4 outColor;

struct MaterialData
{
    uvec2 baseColHandle;
};

layout (std430, binding = 1)
readonly buffer MaterialBuffer
{
    MaterialData materials[];
};

void main()
{
    uvec2 handle = materials[vDrawID].baseColHandle;
    sampler2D baseColSampler = sampler2D(handle);
    
    outColor = texture(baseColSampler, vTexCoord);
}