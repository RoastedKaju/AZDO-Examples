// Pass body. Reads transmittance_texture (unit 0).
layout(local_size_x = 8, local_size_y = 8) in;

layout(rgba32f, binding = 0) uniform writeonly image2D out_delta_irradiance;

void main() {
    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (any(greaterThanEqual(id, ivec2(IRRADIANCE_W, IRRADIANCE_H)))) return;

    vec2 uv = (vec2(id) + 0.5) / vec2(IRRADIANCE_W, IRRADIANCE_H);
    float r, mu_s;
    GetRMuSFromIrradianceTextureUv(uv, r, mu_s);

    imageStore(out_delta_irradiance, id, vec4(ComputeDirectIrradiance(r, mu_s), 1.0));
}
