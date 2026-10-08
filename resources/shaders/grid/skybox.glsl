#version 460 core

#extension GL_ARB_bindless_texture : require

layout (location = 0) in vec3 direction;

layout (location = 0) out vec4 color;

layout (binding = 0)
uniform CubemapData
{
    uvec2 cubemapHandle;
};

void main()
{
    samplerCube cubemapSampler = samplerCube(cubemapHandle);
    vec3 hdr = texture(cubemapSampler, normalize(direction)).rgb;

    float exposure = 1.0;
    vec3 mapped = vec3(1.0) - exp(-hdr * exposure);
    mapped = pow(mapped, vec3(1.0/2.2));

    color = vec4(mapped, 1.0);
}