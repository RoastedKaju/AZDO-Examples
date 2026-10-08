#version 460 core

layout (std140, binding = 1)
uniform Camera
{
    mat4 uView;
    mat4 uProjection;
    mat4 uInvViewProjection; // inverse(uProjection * uView)
    vec4 uCameraPosition;
};

layout (location = 0) out vec3 vNearPoint;
layout (location = 1) out vec3 vFarPoint;

const vec2 kFullscreenQuad[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
);

vec3 unproject(vec2 ndcXY, float ndcZ)
{
    vec4 p = uInvViewProjection * vec4(ndcXY, ndcZ, 1.0);
    return p.xyz / p.w;
}

void main()
{
    vec2 ndc = kFullscreenQuad[gl_VertexID];

    // Default OpenGL depth convention: NDC z is [-1, 1].
    vNearPoint = unproject(ndc, -1.0);
    vFarPoint  = unproject(ndc,  1.0);

    gl_Position = vec4(ndc, 0.0, 1.0);
}