// Pass body. C++ prepends "#version 460 core" + atmosphere_common.glsl.
layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba32f, binding = 0) uniform writeonly image2D out_transmittance;

void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(id, ivec2(TRANSMITTANCE_W, TRANSMITTANCE_H)))) return;

    vec2 uv = (vec2(id) + 0.5) / vec2(TRANSMITTANCE_W, TRANSMITTANCE_H);
    float r, mu;
    GetRMuFromTransmittanceTextureUv(uv, r, mu);

    imageStore(out_transmittance, id, vec4(ComputeTransmittanceToTopAtmosphereBoundary(r, mu), 1.0));
}
