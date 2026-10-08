#version 460 core

#extension GL_ARB_bindless_texture : require

layout (location = 0) in vec3 direction;

layout (location = 0) out vec4 color;

layout (binding = 0)
uniform Panorama
{
    uvec2 panoramaHandle;
};

vec2 uvFromDirection(vec3 d)
{
	d = normalize(d);
	return vec2(atan(d.z,d.x)*0.15915494309+0.5, asin(clamp(d.y,-1.,1.))*0.31830988618+0.5);
}

void main()
{
    sampler2D panoramaSampler = sampler2D(panoramaHandle);
	color = vec4(texture(panoramaSampler,uvFromDirection(direction)).rgb,1.0);
}