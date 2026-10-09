#version 460 core

layout(location = 0) out vec2 v_uv;

void main() {
    // Fullscreen triangle from gl_VertexID, no vertex buffer needed.
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
