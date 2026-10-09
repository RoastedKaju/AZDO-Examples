// Pass body. Reads transmittance_texture (unit 0). One thread per texel of the 3D table.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rgba32f, binding = 0) uniform writeonly image3D out_delta_rayleigh;
layout(rgba32f, binding = 1) uniform writeonly image3D out_delta_mie;
layout(rgba32f, binding = 2) uniform writeonly image3D out_scattering;  // rgb = rayleigh, a = mie.r

void main() {
    ivec3 id = ivec3(gl_GlobalInvocationID);
    ivec3 size = ivec3(SCATTERING_NU_SIZE * SCATTERING_MU_S_SIZE, SCATTERING_MU_SIZE, SCATTERING_R_SIZE);
    if (any(greaterThanEqual(id, size))) return;

    float r, mu, mu_s, nu;
    bool intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(vec3(id) + 0.5, r, mu, mu_s, nu, intersects_ground);

    vec3 rayleigh, mie;
    ComputeSingleScattering(r, mu, mu_s, nu, intersects_ground, rayleigh, mie);

    imageStore(out_delta_rayleigh, id, vec4(rayleigh, 1.0));
    imageStore(out_delta_mie,      id, vec4(mie, 1.0));
    imageStore(out_scattering,     id, vec4(rayleigh, mie.r));
}
