// GI irradiance probes — fixed camera-anchored volume (replaces the world-space hash grid).
//
// A GI_PROBE_DIM^3 lattice of SH-L1 probes covers a cube centered on the camera, snapped to
// GI_PROBE_SPACING. Probe (x,y,z) in [0,DIM) maps directly to buffer slot x + y*DIM + z*DIM^2 — no
// hashing, no allocation, no per-probe lookups. The volume's integer min corner (in probe-coordinate
// units, i.e. world/spacing) is supplied by the caller as `volumeMin`.
//
// Requires PROBE_SH_NAME (float[]) defined by the includer, and PI (shared.inc.glsl). Constants must
// match RendererVKLayout (Layout.ixx). Define GI_PROBE_WRITE for the trace's blend helper.

#ifndef GI_PROBE_INC_GLSL
#define GI_PROBE_INC_GLSL

#define GI_PROBE_DIM     32
#define GI_PROBE_SPACING 4.0
#define GI_SH_STRIDE     12

vec4 shBasisL1(vec3 d)
{
    return vec4(0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x);
}

uint giProbeSlot(ivec3 local) // local in [0,DIM)
{
    return uint(local.x + local.y * GI_PROBE_DIM + local.z * GI_PROBE_DIM * GI_PROBE_DIM);
}

bool giInVolume(ivec3 local)
{
    return all(greaterThanEqual(local, ivec3(0))) && all(lessThan(local, ivec3(GI_PROBE_DIM)));
}

// Cosine-convolved irradiance E(n) from one probe's SH-L1. Diffuse exit radiance is albedo/PI * E(n).
vec3 evalProbeSlot(uint slot, vec3 n)
{
    uint base = slot * GI_SH_STRIDE;
    vec3 c0 = vec3(PROBE_SH_NAME[base + 0u], PROBE_SH_NAME[base + 1u], PROBE_SH_NAME[base + 2u]);
    vec3 c1 = vec3(PROBE_SH_NAME[base + 3u], PROBE_SH_NAME[base + 4u], PROBE_SH_NAME[base + 5u]);
    vec3 c2 = vec3(PROBE_SH_NAME[base + 6u], PROBE_SH_NAME[base + 7u], PROBE_SH_NAME[base + 8u]);
    vec3 c3 = vec3(PROBE_SH_NAME[base + 9u], PROBE_SH_NAME[base + 10u], PROBE_SH_NAME[base + 11u]);
    vec4 Y = shBasisL1(n);
    const float A0 = PI;
    const float A1 = 2.0 * PI / 3.0;
    vec3 E = A0 * c0 * Y.x + A1 * (c1 * Y.y + c2 * Y.z + c3 * Y.w);
    return max(E, vec3(0.0));
}

// Trilinear irradiance from the 8 nearest probes in the fixed volume. Probe k sits at world k*spacing,
// so volumeMin is the probe coordinate of the volume's min corner. Returns vec3(-1) outside the volume.
vec3 evalProbeSH(vec3 worldPos, vec3 n, ivec3 volumeMin)
{
    vec3 p = worldPos + n * (GI_PROBE_SPACING * 0.5); // push along the normal to limit self-leak
    vec3 pf = p / GI_PROBE_SPACING;                   // probe-coordinate space
    ivec3 baseCoord = ivec3(floor(pf));
    vec3 frac = pf - vec3(baseCoord);

    vec3 result = vec3(0.0);
    float totalW = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        vec3 w3 = mix(1.0 - frac, frac, vec3(off));
        float w = w3.x * w3.y * w3.z;
        if (w <= 0.0)
            continue;
        ivec3 local = (baseCoord + off) - volumeMin;
        if (!giInVolume(local))
            continue;
        result += w * evalProbeSlot(giProbeSlot(local), n);
        totalW += w;
    }
    if (totalW > 1e-4)
        return result / totalW;
    return vec3(-1.0);
}

#ifdef GI_PROBE_WRITE
// Temporally blend this frame's freshly-projected coefficients into probe `slot`.
void probeBlendSH(uint slot, vec3 c0, vec3 c1, vec3 c2, vec3 c3, float alpha)
{
    uint base = slot * GI_SH_STRIDE;
    PROBE_SH_NAME[base + 0u]  = mix(PROBE_SH_NAME[base + 0u],  c0.r, alpha);
    PROBE_SH_NAME[base + 1u]  = mix(PROBE_SH_NAME[base + 1u],  c0.g, alpha);
    PROBE_SH_NAME[base + 2u]  = mix(PROBE_SH_NAME[base + 2u],  c0.b, alpha);
    PROBE_SH_NAME[base + 3u]  = mix(PROBE_SH_NAME[base + 3u],  c1.r, alpha);
    PROBE_SH_NAME[base + 4u]  = mix(PROBE_SH_NAME[base + 4u],  c1.g, alpha);
    PROBE_SH_NAME[base + 5u]  = mix(PROBE_SH_NAME[base + 5u],  c1.b, alpha);
    PROBE_SH_NAME[base + 6u]  = mix(PROBE_SH_NAME[base + 6u],  c2.r, alpha);
    PROBE_SH_NAME[base + 7u]  = mix(PROBE_SH_NAME[base + 7u],  c2.g, alpha);
    PROBE_SH_NAME[base + 8u]  = mix(PROBE_SH_NAME[base + 8u],  c2.b, alpha);
    PROBE_SH_NAME[base + 9u]  = mix(PROBE_SH_NAME[base + 9u],  c3.r, alpha);
    PROBE_SH_NAME[base + 10u] = mix(PROBE_SH_NAME[base + 10u], c3.g, alpha);
    PROBE_SH_NAME[base + 11u] = mix(PROBE_SH_NAME[base + 11u], c3.b, alpha);
}
#endif

#endif // GI_PROBE_INC_GLSL
