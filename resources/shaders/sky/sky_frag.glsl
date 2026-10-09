// Pass body for the sky fragment shader. Compose() prepends "#version 460 core" + atmosphere_common.glsl.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

layout(binding = 2) uniform sampler3D scattering_texture;   // rgb = rayleigh, a = mie.r

layout(std140, binding = 1) uniform FrameUBO {
    vec4 camera_pos_exposure;  // xyz = camera position relative to planet center (m), w = exposure
    vec4 cam_right;            // xyz scaled by aspect * tan(fov/2)
    vec4 cam_up;               // xyz scaled by tan(fov/2)
    vec4 cam_forward;
    vec4 sun_dir;
} frame;

// Recovers colored single Mie scattering from the alpha channel (valid since Mie is stored as mie.r only).
vec3 GetExtrapolatedSingleMieScattering(vec4 scattering) {
    if (scattering.r == 0.0) return vec3(0.0);
    return scattering.rgb * scattering.a / scattering.r
         * (atm.rayleigh_scattering.r / atm.mie_scattering.r)
         * (atm.mie_scattering.xyz / atm.rayleigh_scattering.xyz);
}

// Looks up the 4D table with manual interpolation along nu (stored as 8 tiles side by side).
vec3 GetCombinedScattering(float r, float mu, float mu_s, float nu,
                           bool ray_r_mu_intersects_ground, out vec3 single_mie_scattering) {
    vec4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    float tex_coord_x = uvwz.x * float(SCATTERING_NU_SIZE - 1);
    float tex_x = floor(tex_coord_x);
    float lerp_t = tex_coord_x - tex_x;
    vec3 uvw0 = vec3((tex_x + uvwz.y) / float(SCATTERING_NU_SIZE), uvwz.z, uvwz.w);
    vec3 uvw1 = vec3((tex_x + 1.0 + uvwz.y) / float(SCATTERING_NU_SIZE), uvwz.z, uvwz.w);
    vec4 combined = texture(scattering_texture, uvw0) * (1.0 - lerp_t)
                  + texture(scattering_texture, uvw1) * lerp_t;
    single_mie_scattering = GetExtrapolatedSingleMieScattering(combined);
    return combined.rgb;
}

// camera is relative to the planet center. view_ray and sun_direction are unit vectors.
vec3 GetSkyRadiance(vec3 camera, vec3 view_ray, vec3 sun_direction, out vec3 transmittance) {
    float r   = length(camera);
    float rmu = dot(camera, view_ray);
    float top = atm.radii.y;

    // Move the camera to the top atmosphere boundary if it is outside.
    float distance_to_top = -rmu - sqrt(rmu * rmu - r * r + top * top);
    if (distance_to_top > 0.0) {
        camera += view_ray * distance_to_top;
        r = top;
        rmu += distance_to_top;
    } else if (r > top) {
        transmittance = vec3(1.0);
        return vec3(0.0);
    }

    float mu   = rmu / r;
    float mu_s = dot(camera, sun_direction) / r;
    float nu   = dot(view_ray, sun_direction);
    bool intersects_ground = RayIntersectsGround(r, mu);

    transmittance = intersects_ground ? vec3(0.0) : GetTransmittanceToTopAtmosphereBoundary(r, mu);
    vec3 single_mie;
    vec3 scattering = GetCombinedScattering(r, mu, mu_s, nu, intersects_ground, single_mie);
    return scattering * RayleighPhaseFunction(nu) + single_mie * MiePhaseFunction(atm.mie_extinction.w, nu);
}

void main() {
    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 view_ray = normalize(frame.cam_forward.xyz + ndc.x * frame.cam_right.xyz + ndc.y * frame.cam_up.xyz);
    vec3 sun = normalize(frame.sun_dir.xyz);

    vec3 transmittance;
    vec3 radiance = GetSkyRadiance(frame.camera_pos_exposure.xyz, view_ray, sun, transmittance);

    // Sun disc
    float sun_r = atm.solar_irradiance.w;
    if (dot(view_ray, sun) > cos(sun_r)) {
        radiance += transmittance * atm.solar_irradiance.xyz / (PI * sun_r * sun_r);
    }

    // Simple exponential tone mapping + gamma
    out_color = vec4(pow(vec3(1.0) - exp(-radiance * frame.camera_pos_exposure.w), vec3(1.0 / 2.2)), 1.0);
}
