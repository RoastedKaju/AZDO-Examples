#version 460 core

#extension GL_ARB_bindless_texture : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_ARB_shader_draw_parameters : enable

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

layout (location = 0) out vec2 vTexCoord;
layout (location = 1) flat out int vDrawID;

layout (std140, binding = 0)
uniform PerFrameData
{
    mat4 uModelViewProj;
};

void main()
{
    vTexCoord = aTexCoord;
    vDrawID = gl_DrawID;
    gl_Position = uModelViewProj * vec4(aPos, 1.0);
}