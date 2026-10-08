#version 460 core

layout(std140, binding = 1) uniform Camera
{
    mat4 uView;
    mat4 uProjection;
    mat4 uInvViewProjection;
    vec4 uCameraPosition;
};

layout (location = 0) in vec3 vNearPoint;
layout (location = 1) in vec3 vFarPoint;

layout(location = 0) out vec4 outColor;

float gridLine(vec2 worldXZ, float spacing)
{
    vec2 coord = worldXZ / spacing;
    vec2 derivative = fwidth(coord);

    // Distance to nearest grid line, expressed in pixels.
    vec2 line = abs(fract(coord - 0.5) - 0.5) / derivative;
    return 1.0 - min(min(line.x, line.y), 1.0);
}

void main()
{
    vec3 ray = vFarPoint - vNearPoint;

    // Intersection with y = 0 (the XZ grid plane).
    float t = -vNearPoint.y / ray.y;
    if (t <= 0.0)
        discard;

    vec3 worldPos = vNearPoint + t * ray;

    // Discard intersections beyond the camera far plane.
    vec4 clip = uProjection * uView * vec4(worldPos, 1.0);
    float ndcDepth = clip.z / clip.w;
    if (ndcDepth < -1.0 || ndcDepth > 1.0)
        discard;

    // Convert OpenGL's default NDC depth [-1, 1] to window depth [0, 1].
    gl_FragDepth = ndcDepth * 0.5 + 0.5;

    float minor = gridLine(worldPos.xz, 1.0);
    float major = gridLine(worldPos.xz, 10.0);

    float xAxis = 1.0 - smoothstep(0.0, fwidth(worldPos.z) * 1.5, abs(worldPos.z));
    float zAxis = 1.0 - smoothstep(0.0, fwidth(worldPos.x) * 1.5, abs(worldPos.x));

    vec3 minorColor = vec3(0.50, 0.50, 0.50);
    vec3 majorColor = vec3(0.20, 0.20, 0.20);
    vec3 xAxisColor = vec3(1.00, 0.10, 0.10);
    vec3 zAxisColor = vec3(0.10, 0.10, 1.00);

    vec3 color = mix(minorColor, majorColor, major);
    color = mix(color, xAxisColor, xAxis); // X axis
    color = mix(color, zAxisColor, zAxis); // Z axis

    float distanceToCamera = length(worldPos.xz - uCameraPosition.xz);

    // Controls where the fading starts and become completely invisible
    float fade = 1.0 - smoothstep(20.0, 80.0, distanceToCamera);
    float alpha = max(max(minor, major), max(xAxis, zAxis)) * fade;

    if (alpha < 0.002)
        discard;

    outColor = vec4(color, alpha);
}