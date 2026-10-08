#version 460 core

layout (location = 0) in vec3 position;

layout (location = 0) out vec3 direction;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    direction = position;
    gl_Position = projection * view * vec4(position, 1.0);
}