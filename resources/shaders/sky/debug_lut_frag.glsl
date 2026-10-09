#version 460 core
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(location = 0) uniform int u_mode;   // 0 transmittance, 1 direct irradiance, 2 scattering slice
layout(location = 1) uniform int u_layer;  // scattering slice (altitude layer)

layout(binding = 0) uniform sampler2D u_transmittance;
layout(binding = 1) uniform sampler2D u_irradiance;
layout(binding = 2) uniform sampler3D u_scattering;

void main() {
    vec3 c;
    if (u_mode == 0) {
        c = texture(u_transmittance, v_uv).rgb;
    } else if (u_mode == 1) {
        c = 1.0 - exp(-texture(u_irradiance, v_uv).rgb);
    } else {
        ivec3 size = textureSize(u_scattering, 0);
        ivec3 p = ivec3(min(ivec2(v_uv * vec2(size.xy)), size.xy - 1), u_layer);
        c = 1.0 - exp(-4.0 * texelFetch(u_scattering, p, 0).rgb);   // arbitrary exposure, debug only
    }
    out_color = vec4(c, 1.0);
}
