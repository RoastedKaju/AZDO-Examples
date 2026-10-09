// Shared atmosphere code (Bruneton 2017). Body only: C++ prepends "#version 460 core".
// All lengths in meters. r = distance from planet center, mu = cos(view zenith),
// mu_s = cos(sun zenith), nu = cos(angle between view and sun).

// Keep these in sync with main.cpp.
const int TRANSMITTANCE_W        = 256;
const int TRANSMITTANCE_H        = 64;
const int SCATTERING_R_SIZE      = 32;
const int SCATTERING_MU_SIZE     = 128;
const int SCATTERING_MU_S_SIZE   = 32;
const int SCATTERING_NU_SIZE     = 8;
const int IRRADIANCE_W           = 64;
const int IRRADIANCE_H           = 16;
const float PI = 3.14159265358979323846;

layout(std140, binding = 0) uniform AtmosphereUBO {
    vec4 rayleigh_scattering;    // xyz = coefficient (1/m), w = scale height (m)
    vec4 mie_scattering;         // xyz = coefficient (1/m), w = scale height (m)
    vec4 mie_extinction;         // xyz = coefficient (1/m), w = phase asymmetry g
    vec4 absorption_extinction;  // xyz = ozone extinction at peak (1/m), w = layer center altitude (m)
    vec4 solar_irradiance;       // xyz = irradiance, w = sun angular radius (rad)
    vec4 radii;                  // x = bottom, y = top, z = mu_s_min, w = ozone half width (m)
    vec4 ground_albedo;          // xyz (unused for now)
} atm;

layout(binding = 0) uniform sampler2D transmittance_texture;

// ---------------------------------------------------------------------------
// Basic helpers
// ---------------------------------------------------------------------------
float ClampCosine(float mu)  { return clamp(mu, -1.0, 1.0); }
float ClampDistance(float d) { return max(d, 0.0); }
float ClampRadius(float r)   { return clamp(r, atm.radii.x, atm.radii.y); }
float SafeSqrt(float a)      { return sqrt(max(a, 0.0)); }

float DistanceToTopAtmosphereBoundary(float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + atm.radii.y * atm.radii.y;
    return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

float DistanceToBottomAtmosphereBoundary(float r, float mu) {
    float discriminant = r * r * (mu * mu - 1.0) + atm.radii.x * atm.radii.x;
    return ClampDistance(-r * mu - SafeSqrt(discriminant));
}

bool RayIntersectsGround(float r, float mu) {
    return mu < 0.0 && r * r * (mu * mu - 1.0) + atm.radii.x * atm.radii.x >= 0.0;
}

float DistanceToNearestAtmosphereBoundary(float r, float mu, bool intersects_ground) {
    return intersects_ground ? DistanceToBottomAtmosphereBoundary(r, mu)
                             : DistanceToTopAtmosphereBoundary(r, mu);
}

// Maps a unit-range value to a texture coordinate that hits texel centers at 0 and 1.
float GetTextureCoordFromUnitRange(float x, float texture_size) {
    return 0.5 / texture_size + x * (1.0 - 1.0 / texture_size);
}
float GetUnitRangeFromTextureCoord(float u, float texture_size) {
    return (u - 0.5 / texture_size) / (1.0 - 1.0 / texture_size);
}

// ---------------------------------------------------------------------------
// Density profiles
// ---------------------------------------------------------------------------
// x = rayleigh, y = mie, z = ozone (relative densities)
vec3 Densities(float altitude) {
    float rayleigh = exp(-altitude / atm.rayleigh_scattering.w);
    float mie      = exp(-altitude / atm.mie_scattering.w);
    float ozone    = max(0.0, 1.0 - abs(altitude - atm.absorption_extinction.w) / atm.radii.w);
    return vec3(rayleigh, mie, ozone);
}

vec3 ExtinctionAt(float altitude) {
    vec3 d = Densities(altitude);
    return atm.rayleigh_scattering.xyz   * d.x
         + atm.mie_extinction.xyz        * d.y
         + atm.absorption_extinction.xyz * d.z;
}

// ---------------------------------------------------------------------------
// Transmittance table: parameterization and computation
// ---------------------------------------------------------------------------
vec2 GetTransmittanceTextureUvFromRMu(float r, float mu) {
    float H   = sqrt(atm.radii.y * atm.radii.y - atm.radii.x * atm.radii.x);
    float rho = SafeSqrt(r * r - atm.radii.x * atm.radii.x);
    float d     = DistanceToTopAtmosphereBoundary(r, mu);
    float d_min = atm.radii.y - r;
    float d_max = rho + H;
    float x_mu  = (d - d_min) / (d_max - d_min);
    float x_r   = rho / H;
    return vec2(GetTextureCoordFromUnitRange(x_mu, float(TRANSMITTANCE_W)),
                GetTextureCoordFromUnitRange(x_r,  float(TRANSMITTANCE_H)));
}

void GetRMuFromTransmittanceTextureUv(vec2 uv, out float r, out float mu) {
    float x_mu = GetUnitRangeFromTextureCoord(uv.x, float(TRANSMITTANCE_W));
    float x_r  = GetUnitRangeFromTextureCoord(uv.y, float(TRANSMITTANCE_H));
    float H   = sqrt(atm.radii.y * atm.radii.y - atm.radii.x * atm.radii.x);
    float rho = H * x_r;
    r = sqrt(rho * rho + atm.radii.x * atm.radii.x);
    float d_min = atm.radii.y - r;
    float d_max = rho + H;
    float d = d_min + x_mu * (d_max - d_min);
    mu = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = ClampCosine(mu);
}

vec3 ComputeTransmittanceToTopAtmosphereBoundary(float r, float mu) {
    const int SAMPLE_COUNT = 500;
    float d  = DistanceToTopAtmosphereBoundary(r, mu);
    float dx = d / float(SAMPLE_COUNT);
    vec3 optical_depth = vec3(0.0);
    for (int i = 0; i <= SAMPLE_COUNT; ++i) {
        float d_i = float(i) * dx;
        float r_i = sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r);
        float weight = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;  // trapezoid rule
        optical_depth += ExtinctionAt(r_i - atm.radii.x) * weight * dx;
    }
    return exp(-optical_depth);
}

// ---------------------------------------------------------------------------
// Transmittance table lookups
// ---------------------------------------------------------------------------
vec3 GetTransmittanceToTopAtmosphereBoundary(float r, float mu) {
    return texture(transmittance_texture, GetTransmittanceTextureUvFromRMu(r, mu)).rgb;
}

// Transmittance between (r, mu) and the point at distance d along the ray.
vec3 GetTransmittance(float r, float mu, float d, bool ray_r_mu_intersects_ground) {
    float r_d  = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_d = ClampCosine((r * mu + d) / r_d);
    vec3 t;
    if (ray_r_mu_intersects_ground) {
        t = GetTransmittanceToTopAtmosphereBoundary(r_d, -mu_d)
          / GetTransmittanceToTopAtmosphereBoundary(r,   -mu);
    } else {
        t = GetTransmittanceToTopAtmosphereBoundary(r,   mu)
          / GetTransmittanceToTopAtmosphereBoundary(r_d, mu_d);
    }
    return min(t, vec3(1.0));
}

// Transmittance to the sun, including the soft planet-horizon cutoff.
vec3 GetTransmittanceToSun(float r, float mu_s) {
    float sin_theta_h = atm.radii.x / r;
    float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));
    float sun_r = atm.solar_irradiance.w;
    return GetTransmittanceToTopAtmosphereBoundary(r, mu_s)
         * smoothstep(-sin_theta_h * sun_r, sin_theta_h * sun_r, mu_s - cos_theta_h);
}

// ---------------------------------------------------------------------------
// Direct irradiance
// ---------------------------------------------------------------------------
vec3 ComputeDirectIrradiance(float r, float mu_s) {
    float alpha_s = atm.solar_irradiance.w;
    // Average of max(mu_s, 0) over the sun disc.
    float average_cosine_factor =
        mu_s < -alpha_s ? 0.0 :
        (mu_s > alpha_s ? mu_s : (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s));
    return atm.solar_irradiance.xyz
         * GetTransmittanceToTopAtmosphereBoundary(r, mu_s)
         * average_cosine_factor;
}

void GetRMuSFromIrradianceTextureUv(vec2 uv, out float r, out float mu_s) {
    float x_mu_s = GetUnitRangeFromTextureCoord(uv.x, float(IRRADIANCE_W));
    float x_r    = GetUnitRangeFromTextureCoord(uv.y, float(IRRADIANCE_H));
    r    = atm.radii.x + x_r * (atm.radii.y - atm.radii.x);
    mu_s = ClampCosine(2.0 * x_mu_s - 1.0);
}

// ---------------------------------------------------------------------------
// Phase functions
// ---------------------------------------------------------------------------
float RayleighPhaseFunction(float nu) {
    return 3.0 / (16.0 * PI) * (1.0 + nu * nu);
}

float MiePhaseFunction(float g, float nu) {
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + nu * nu) / pow(1.0 + g * g - 2.0 * g * nu, 1.5);
}

// ---------------------------------------------------------------------------
// Scattering table parameterization: 4D (r, mu, mu_s, nu) <-> 3D texture
// uvwz = (nu, mu_s, mu, r). The 3D texture is NU_SIZE*MU_S_SIZE wide, MU_SIZE high, R_SIZE deep.
// ---------------------------------------------------------------------------
vec4 GetScatteringTextureUvwzFromRMuMuSNu(float r, float mu, float mu_s, float nu,
                                          bool ray_r_mu_intersects_ground) {
    float H   = sqrt(atm.radii.y * atm.radii.y - atm.radii.x * atm.radii.x);
    float rho = SafeSqrt(r * r - atm.radii.x * atm.radii.x);
    float u_r = GetTextureCoordFromUnitRange(rho / H, float(SCATTERING_R_SIZE));

    float r_mu = r * mu;
    float discriminant = r_mu * r_mu - r * r + atm.radii.x * atm.radii.x;
    float u_mu;
    if (ray_r_mu_intersects_ground) {
        float d     = -r_mu - SafeSqrt(discriminant);
        float d_min = r - atm.radii.x;
        float d_max = rho;
        u_mu = 0.5 - 0.5 * GetTextureCoordFromUnitRange(
            d_max == d_min ? 0.0 : (d - d_min) / (d_max - d_min), float(SCATTERING_MU_SIZE / 2));
    } else {
        float d     = -r_mu + SafeSqrt(discriminant + H * H);
        float d_min = atm.radii.y - r;
        float d_max = rho + H;
        u_mu = 0.5 + 0.5 * GetTextureCoordFromUnitRange(
            (d - d_min) / (d_max - d_min), float(SCATTERING_MU_SIZE / 2));
    }

    float d     = DistanceToTopAtmosphereBoundary(atm.radii.x, mu_s);
    float d_min = atm.radii.y - atm.radii.x;
    float d_max = H;
    float a = (d - d_min) / (d_max - d_min);
    float D = DistanceToTopAtmosphereBoundary(atm.radii.x, atm.radii.z);
    float A = (D - d_min) / (d_max - d_min);
    float u_mu_s = GetTextureCoordFromUnitRange(
        max(1.0 - a / A, 0.0) / (1.0 + a), float(SCATTERING_MU_S_SIZE));

    float u_nu = (nu + 1.0) / 2.0;
    return vec4(u_nu, u_mu_s, u_mu, u_r);
}

void GetRMuMuSNuFromScatteringTextureUvwz(vec4 uvwz, out float r, out float mu,
                                          out float mu_s, out float nu,
                                          out bool ray_r_mu_intersects_ground) {
    float H   = sqrt(atm.radii.y * atm.radii.y - atm.radii.x * atm.radii.x);
    float rho = H * GetUnitRangeFromTextureCoord(uvwz.w, float(SCATTERING_R_SIZE));
    r = sqrt(rho * rho + atm.radii.x * atm.radii.x);

    if (uvwz.z < 0.5) {
        float d_min = r - atm.radii.x;
        float d_max = rho;
        float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            1.0 - 2.0 * uvwz.z, float(SCATTERING_MU_SIZE / 2));
        mu = (d == 0.0) ? -1.0 : ClampCosine(-(rho * rho + d * d) / (2.0 * r * d));
        ray_r_mu_intersects_ground = true;
    } else {
        float d_min = atm.radii.y - r;
        float d_max = rho + H;
        float d = d_min + (d_max - d_min) * GetUnitRangeFromTextureCoord(
            2.0 * uvwz.z - 1.0, float(SCATTERING_MU_SIZE / 2));
        mu = (d == 0.0) ? 1.0 : ClampCosine((H * H - rho * rho - d * d) / (2.0 * r * d));
        ray_r_mu_intersects_ground = false;
    }

    float x_mu_s = GetUnitRangeFromTextureCoord(uvwz.y, float(SCATTERING_MU_S_SIZE));
    float d_min = atm.radii.y - atm.radii.x;
    float d_max = H;
    float D = DistanceToTopAtmosphereBoundary(atm.radii.x, atm.radii.z);
    float A = (D - d_min) / (d_max - d_min);
    float a = (A - x_mu_s * A) / (1.0 + x_mu_s * A);
    float d = d_min + min(a, A) * (d_max - d_min);
    mu_s = (d == 0.0) ? 1.0 : ClampCosine((H * H - d * d) / (2.0 * atm.radii.x * d));

    nu = ClampCosine(uvwz.x * 2.0 - 1.0);
}

// frag_coord = texel center in the 3D texture (x + 0.5, y + 0.5, layer + 0.5).
void GetRMuMuSNuFromScatteringTextureFragCoord(vec3 frag_coord, out float r, out float mu,
                                               out float mu_s, out float nu,
                                               out bool ray_r_mu_intersects_ground) {
    const vec4 SIZE = vec4(float(SCATTERING_NU_SIZE - 1), float(SCATTERING_MU_S_SIZE),
                           float(SCATTERING_MU_SIZE), float(SCATTERING_R_SIZE));
    float frag_nu   = floor(frag_coord.x / float(SCATTERING_MU_S_SIZE));
    float frag_mu_s = mod(frag_coord.x, float(SCATTERING_MU_S_SIZE));
    vec4 uvwz = vec4(frag_nu, frag_mu_s, frag_coord.y, frag_coord.z) / SIZE;
    GetRMuMuSNuFromScatteringTextureUvwz(uvwz, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    // nu is only valid within a range determined by mu and mu_s.
    float s = sqrt((1.0 - mu * mu) * (1.0 - mu_s * mu_s));
    nu = clamp(nu, mu * mu_s - s, mu * mu_s + s);
}

// ---------------------------------------------------------------------------
// Single scattering (phase functions are applied at lookup time, not stored)
// ---------------------------------------------------------------------------
void ComputeSingleScatteringIntegrand(float r, float mu, float mu_s, float nu, float d,
                                      bool ray_r_mu_intersects_ground,
                                      out vec3 rayleigh, out vec3 mie) {
    float r_d    = ClampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r));
    float mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);
    vec3 transmittance = GetTransmittance(r, mu, d, ray_r_mu_intersects_ground)
                       * GetTransmittanceToSun(r_d, mu_s_d);
    vec3 dens = Densities(r_d - atm.radii.x);
    rayleigh = transmittance * dens.x;
    mie      = transmittance * dens.y;
}

void ComputeSingleScattering(float r, float mu, float mu_s, float nu,
                             bool ray_r_mu_intersects_ground,
                             out vec3 rayleigh, out vec3 mie) {
    const int SAMPLE_COUNT = 50;
    float dx = DistanceToNearestAtmosphereBoundary(r, mu, ray_r_mu_intersects_ground)
             / float(SAMPLE_COUNT);
    vec3 rayleigh_sum = vec3(0.0);
    vec3 mie_sum      = vec3(0.0);
    for (int i = 0; i <= SAMPLE_COUNT; ++i) {
        float d_i = float(i) * dx;
        vec3 rayleigh_i, mie_i;
        ComputeSingleScatteringIntegrand(r, mu, mu_s, nu, d_i, ray_r_mu_intersects_ground,
                                         rayleigh_i, mie_i);
        float weight = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
        rayleigh_sum += rayleigh_i * weight;
        mie_sum      += mie_i * weight;
    }
    rayleigh = rayleigh_sum * dx * atm.solar_irradiance.xyz * atm.rayleigh_scattering.xyz;
    mie      = mie_sum      * dx * atm.solar_irradiance.xyz * atm.mie_scattering.xyz;
}
