#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require

layout (location = 0) in vec2 vTexCoord;
layout (location = 1) flat in int vMaterialIndex;

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
    // fetch material handle using the instance's material index
    uvec2 handle = materials[vMaterialIndex].baseColHandle;
    
    // construct bindless sampler and sample texture
    sampler2D baseColSampler = sampler2D(handle);
    outColor = texture(baseColSampler, vTexCoord);
}