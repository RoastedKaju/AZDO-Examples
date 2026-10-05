#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_ARB_shader_draw_parameters : require

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out int vMaterialIndex;

layout (std140, binding = 0)
uniform PerFrameData
{
    mat4 uViewProj;
};

struct PerInstanceData
{
    mat4 world_matrix;
    int material_index;
};

layout(std430, binding = 2)
readonly buffer InstanceBuffer
{
    PerInstanceData instances[];
};

void main()
{
    PerInstanceData instance = instances[gl_BaseInstance + gl_InstanceID];

    vTexCoord = aTexCoord;
    vMaterialIndex = instance.material_index;

    vec4 world_pos = instance.world_matrix * vec4(aPos, 1.0);
    gl_Position = uViewProj * world_pos;
}