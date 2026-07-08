#ifndef OCEAN_WAVE_INC_GLSL
#define OCEAN_WAVE_INC_GLSL

// Gerstner-spectrum ocean wave field, shared by the displacement passes (instanced_indirect.vs.glsl with
// OCEAN, the G-buffer prepass gbuffer.vs.glsl) and the water shading (ocean.fs.glsl). One source of truth
// so the depth the prepass writes, the geometry the forward pass draws, and the normals the fragment
// shades all agree. Requires ubo.inc.glsl (u_oceanParams0/1, u_timeSeconds); pulled in via shared.inc.glsl.
//
// Two layers, following the usual analytic-ocean recipe (cf. Alekseev's "Seascape", iq's ocean shaders):
//   1. A bank of Gerstner (trochoidal) waves for the large-scale swell + geometry (vertex displacement).
//      Directions are spread WIDE and decorrelated around the wind (a low-discrepancy fan, not a tight
//      cone) with per-wave phase offsets, so the crests cross instead of combing into one repeating ridge.
//   2. A domain-warped value-noise FBM (analytic gradient, per-octave rotated) whose slope perturbs the
//      shading normal per pixel — this is the random high-frequency ripple that stops the surface reading
//      as a few smooth blobs. World-space + wind-drifted, so it never swims and never obviously tiles.

#define OCEAN_VERT_WAVES 5    // Gerstner waves the vertex displacement passes sum (prepass + forward MUST match)
#define OCEAN_FRAG_WAVES 6    // Gerstner waves the fragment sums for the base normal
#define OCEAN_MAX_WAVES  6    // steepness normalisation constant (keeps wave i identical across passes)
#define OCEAN_DETAIL_OCTAVES 5 // noise octaves layered onto the shading normal
const float OCEAN_G = 9.81;   // gravity, for the deep-water dispersion relation

// Integer hash -> [0,1), per-wave (direction/phase jitter).
float oceanHash(int i)
{
    uint n = uint(i) * 747796405u + 2891336453u;
    n = ((n >> ((n >> 28) + 4u)) ^ n) * 277803737u;
    return float((n >> 22) ^ n) * (1.0 / 4294967295.0);
}
// Integer lattice hash -> [0,1) for the value noise (no fract() diagonal-seam correlation).
float oceanHash2(vec2 p)
{
    uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334673u, 3812015801u);
    uint n = (q.x ^ q.y) * 1597334673u;
    return float(n) * (1.0 / 4294967295.0);
}
// Value noise with analytic 2D gradient (iq, "value noise derivatives"): returns vec3(value, d/dx, d/dy).
// The quintic fade is C2, so the gradient is smooth — no cell creases in the detail normal.
vec3 oceanNoised(vec2 x)
{
    vec2 p = floor(x);
    vec2 f = fract(x);
    vec2 u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    vec2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);
    float a = oceanHash2(p);
    float b = oceanHash2(p + vec2(1.0, 0.0));
    float c = oceanHash2(p + vec2(0.0, 1.0));
    float d = oceanHash2(p + vec2(1.0, 1.0));
    float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
    return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y,
                du.x * (k1 + k4 * u.y),
                du.y * (k2 + k4 * u.x));
}

// Parameters of Gerstner octave i, derived from the UBO base params (wind dir, amplitude, choppiness,
// wavelength). Directions are fanned WIDE (+/- ~80 deg) around the wind via the golden-ratio low-discrepancy
// sequence so successive octaves travel in decorrelated directions (crossing wave trains, no corduroy).
void oceanWaveParams(int i, out vec2 dir, out float k, out float w, out float amp, out float steep, out float phase0)
{
    const float baseAmp = u_oceanParams0.z;
    const float chop    = u_oceanParams0.w;
    const float baseLen = max(u_oceanParams1.x, 1.0);
    const vec2  wind    = u_oceanParams0.xy;

    float len = baseLen * pow(0.63, float(i)); // each octave ~37% shorter (non-harmonic: no exact period)
    k   = 6.2831853 / len;
    w   = sqrt(OCEAN_G * k);                    // deep-water dispersion relation, w^2 = g*k

    const float windA = atan(wind.y, wind.x);
    float spread = (fract(float(i) * 0.6180339887 + 0.5) * 2.0 - 1.0) * 1.4; // +/- ~80 deg, low-discrepancy
    float ang = windA + spread;
    dir = vec2(cos(ang), sin(ang));

    // Phillips-spectrum amplitude (Tessendorf 2001, "Simulating Ocean Water"), tempered for the low octave
    // count, times a cos^2 directional-spreading factor (wave energy biased downwind, crossing swells kept
    // at a floor so the wide fan survives). Normalised so octave 0 (downwind, base wavelength) == baseAmp.
    float kp      = 6.2831853 / baseLen;             // peak / base wavenumber
    float rolloff = pow(kp / k, 1.3);                // Phillips ~1/k^2 energy, tempered to ~1/k^1.3
    float align   = dot(dir, wind) * 0.5 + 0.5;      // 0 (upwind) .. 1 (downwind)
    float dirBias = mix(0.35, 1.0, align * align);   // directional spreading, floored to preserve spread
    amp = baseAmp * rolloff * dirBias;

    // Steepness normalised by k*amp so the horizontal choppiness stays bounded regardless of the spectrum.
    steep  = chop / max(k * amp * float(OCEAN_MAX_WAVES), 1e-3);
    phase0 = oceanHash(i) * 6.2831853; // decorrelate the phases so crests don't all align at the origin
}

// Evaluate the Gerstner field at an undisplaced horizontal world position.
//   disp     : Gerstner displacement (xz = horizontal choppiness, y = height) to add to the flat surface
//   nrm      : analytic surface normal (Y-up, normalised) — base (large-scale) normal, detail added in the FS
//   jacobian : determinant of the horizontal-displacement Jacobian; < ~0 where crests fold over -> foam
void oceanEval(vec2 worldXZ, int numWaves, out vec3 disp, out vec3 nrm, out float jacobian)
{
    disp = vec3(0.0);
    float nx = 0.0, ny = 1.0, nz = 0.0;
    float jxx = 0.0, jzz = 0.0, jxz = 0.0;
    const float t = u_timeSeconds * u_oceanParams1.y;

    for (int i = 0; i < numWaves; ++i)
    {
        vec2 dir; float k, w, amp, steep, phase0;
        oceanWaveParams(i, dir, k, w, amp, steep, phase0);

        float phase = k * dot(dir, worldXZ) - w * t + phase0;
        float c = cos(phase), s = sin(phase);

        disp.x += steep * amp * dir.x * c;
        disp.z += steep * amp * dir.y * c;
        disp.y += amp * s;

        float wa = k * amp;
        nx -= dir.x * wa * c;
        nz -= dir.y * wa * c;
        ny -= steep * wa * s;

        float qak = steep * amp * k;
        jxx -= qak * dir.x * dir.x * s;
        jzz -= qak * dir.y * dir.y * s;
        jxz -= qak * dir.x * dir.y * s;
    }

    nrm = normalize(vec3(nx, ny, nz));
    jacobian = (1.0 + jxx) * (1.0 + jzz) - jxz * jxz;
}

// Vertex-side helper: the displacement + coarse normal both raster displacement passes use (identical
// wave count so the prepass reference depth matches the forward geometry).
void oceanVertex(vec2 worldXZ, out vec3 disp, out vec3 nrm)
{
    float jacobian;
    oceanEval(worldXZ, OCEAN_VERT_WAVES, disp, nrm, jacobian);
}

// High-frequency detail slope for the shading normal: a domain-warped, per-octave-rotated value-noise FBM,
// wind-drifted so the ripples move. Returns the world-space height gradient (dh/dx, dh/dz); the fragment
// tilts the base normal by it. Rotating + scaling the domain each octave (det = 4 -> x2 frequency) keeps
// the lattices from aligning, and the low-frequency domain warp on the first sample removes any residual
// grid signature — the result is random, non-tiling ripple detail.
vec2 oceanDetailSlope(vec2 worldXZ)
{
    const float t = u_timeSeconds * u_oceanParams1.y;
    const mat2 rot = mat2(1.6, 1.2, -1.2, 1.6); // rotate + ~2x scale per octave
    const vec2 wind = u_oceanParams0.xy;

    // Start a few times finer than the base swell, tied to wavelength so the ripple scale tracks the sea.
    vec2 p = worldXZ * (6.2831853 / max(u_oceanParams1.x, 1.0)) * 3.0;
    // Low-frequency domain warp so the noise field wanders (no visible base lattice).
    p += (vec2(oceanNoised(p * 0.35 + vec2(1.7, 9.2)).x, oceanNoised(p * 0.35 + vec2(6.1, 3.4)).x) - 0.5) * 1.5;

    vec2 slope = vec2(0.0);
    float amp = 0.5;
    for (int i = 0; i < OCEAN_DETAIL_OCTAVES; ++i)
    {
        vec2 drift = wind * (t * (0.5 + 0.28 * float(i)));
        vec3 n = oceanNoised(p + drift);
        slope += amp * n.yz;
        p = rot * p;
        amp *= 0.55;
    }
    return slope;
}

#endif
