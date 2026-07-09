// GI irradiance probes — a single persistent, world-space CASCADED CLIPMAP volume. GI_NUM_CASCADES nested
// probe grids, each GI_CASCADE_PROBE_DIM^3 probes at a fixed power-of-two spacing (BASE_SPACING << cascade),
// camera-centered. Probes sit at ABSOLUTE lattice positions (lc * spacing) and are stored toroidally
// (slot = lc & (DIM-1)), so a probe that stays in range maps to the same storage slot every frame and its
// SH carries forward in place — no hash table, no copy, no prev/cur ping-pong. When the camera moves, the
// lattice coords that scroll out are silently overwritten by the new coords that wrap into their slots.
//
// Buffers / names the includer must define before including (read side):
//   GI_GRID_DATA_NAME   float[] per-probe data: cascade-major, slot-linear, GI_PROBE_STRIDE floats each.
// For the write side (trace) also define GI_PROBE_WRITE.
//
// Requires shared.inc.glsl (PI) (u_viewPos — the camera, which centers the cascades).

#ifndef GI_PROBE_INC_GLSL
#define GI_PROBE_INC_GLSL

// GI_SH_STRIDE, GI_NUM_CASCADES, GI_CASCADE_PROBE_DIM and GI_CASCADE_BASE_SPACING are injected by the
// engine from RendererVKLayout (Layout.ixx).
#define GI_PROBE_STRIDE (GI_SH_STRIDE + 2) // SH + mean free-space distance at word +12 + backface-hit fraction at +13

#ifndef GI_NORMAL_BIAS
#define GI_NORMAL_BIAS 1.5 // push the sample point along the normal (world units) to limit self-leak
#endif
#ifndef GI_NORMAL_BIAS_SPACING
#define GI_NORMAL_BIAS_SPACING 0.25 // extra normal bias as a fraction of the cascade's probe spacing
#endif
// Probes whose gather rays mostly hit backfaces sit inside (or right behind) geometry; their near-black SH
// would darken every surface in their trilinear footprint. Fade them out of the lookup over this fraction
// range (dead probes renormalize away; the all-dead case falls through to a coarser cascade).
#ifndef GI_BACKFACE_DEAD_MIN
#define GI_BACKFACE_DEAD_MIN 0.15
#endif
#ifndef GI_BACKFACE_DEAD_MAX
#define GI_BACKFACE_DEAD_MAX 0.35
#endif
// -----------------------------------------------------------------------------------------------------

#define GI_CASCADE_PROBES (GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM)
#define GI_DIM_MASK        (GI_CASCADE_PROBE_DIM - 1)

vec4 shBasisL1(vec3 d)
{
    return vec4(0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x);
}

// Probe spacing (world units) of a cascade. Cascade 0 is finest; each level doubles.
int giCascadeSpacing(int c) { return GI_CASCADE_BASE_SPACING << c; }

// Integer lattice coord of the cascade's min corner, snapped so the camera sits at its center.
ivec3 giCascadeOrigin(int c, vec3 camPos)
{
    int s = giCascadeSpacing(c);
    return ivec3(floor(camPos / float(s))) - ivec3(GI_CASCADE_PROBE_DIM / 2);
}

// Toroidal slot (linear) for an absolute lattice coord. lc & mask is a true mod for power-of-two DIM,
// correct for negative coords under two's complement.
uint giSlotLinear(ivec3 lc)
{
    ivec3 s = lc & ivec3(GI_DIM_MASK);
    return uint(s.x + s.y * GI_CASCADE_PROBE_DIM + s.z * GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM);
}

// Word offset into GI_GRID_DATA_NAME for the probe at lattice coord lc in cascade c.
uint giProbeBase(int c, ivec3 lc)
{
    return (uint(c) * uint(GI_CASCADE_PROBES) + giSlotLinear(lc)) * uint(GI_PROBE_STRIDE);
}

// Mean distance from a probe to surrounding geometry (misses counted as the gather range), i.e. its
// free-space radius. Used as a single-moment occlusion estimate at lookup time.
float giProbeMeanDist(uint cellBase) { return GI_GRID_DATA_NAME[cellBase + uint(GI_SH_STRIDE)]; }

// Fraction of the probe's gather rays that hit backfacing geometry (~1 = embedded in a wall/terrain).
float giProbeBackfaceFrac(uint cellBase) { return GI_GRID_DATA_NAME[cellBase + uint(GI_SH_STRIDE) + 1u]; }

// Raw SH-L1 RGB coefficients of one probe.
void giReadSH(uint cellBase, out vec3 c0, out vec3 c1, out vec3 c2, out vec3 c3)
{
    c0 = vec3(GI_GRID_DATA_NAME[cellBase + 0u], GI_GRID_DATA_NAME[cellBase + 1u],  GI_GRID_DATA_NAME[cellBase + 2u]);
    c1 = vec3(GI_GRID_DATA_NAME[cellBase + 3u], GI_GRID_DATA_NAME[cellBase + 4u],  GI_GRID_DATA_NAME[cellBase + 5u]);
    c2 = vec3(GI_GRID_DATA_NAME[cellBase + 6u], GI_GRID_DATA_NAME[cellBase + 7u],  GI_GRID_DATA_NAME[cellBase + 8u]);
    c3 = vec3(GI_GRID_DATA_NAME[cellBase + 9u], GI_GRID_DATA_NAME[cellBase + 10u], GI_GRID_DATA_NAME[cellBase + 11u]);
}

// Cosine-convolved irradiance E(n) from SH-L1 coefficients. Diffuse exit radiance is albedo/PI * E(n).
vec3 giEvalSH(vec3 c0, vec3 c1, vec3 c2, vec3 c3, vec3 n)
{
    vec4 Y = shBasisL1(n);
    const float A0 = PI;
    const float A1 = 2.0 * PI / 3.0;
    vec3 E = A0 * c0 * Y.x + A1 * (c1 * Y.y + c2 * Y.z + c3 * Y.w);
    return max(E, vec3(0.0));
}

vec3 giEvalCell(uint cellBase, vec3 n)
{
    vec3 c0, c1, c2, c3;
    giReadSH(cellBase, c0, c1, c2, c3);
    return giEvalSH(c0, c1, c2, c3, n);
}

// True when cascade c's 8-probe stencil around p fully fits inside its toroidal window (so the slots are
// the ones actually resident for this camera position, and we never interpolate across the wrap seam).
bool giCascadeFits(int c, vec3 p, out ivec3 base, out ivec3 origin, out int s, out vec3 frac)
{
    s      = giCascadeSpacing(c);
    origin = giCascadeOrigin(c, u_viewPos);
    vec3 pf = p / float(s);
    base   = ivec3(floor(pf));
    frac   = pf - vec3(base);
    return !(any(lessThan(base, origin)) || any(greaterThanEqual(base + 1, origin + GI_CASCADE_PROBE_DIM)));
}

// Trilinear irradiance from one cascade's 8 nearest probes, with DDGI-style backface weighting (probes
// behind the surface are faded out to limit light leaking through thin geometry). totalW returns the
// summed weight so the caller can detect the all-backfaced case and fall through to a coarser cascade.
vec3 giSampleCascade(int c, int s, ivec3 base, vec3 frac, vec3 worldPos, vec3 n, out float totalW)
{
    // Blend the raw SH-L1 coefficients (not per-probe irradiance) and clamp once at the end. The cosine
    // convolution's max(0) is a per-probe nonlinearity; applying it after interpolation avoids the kinks
    // between probes that show up as ripples on flat surfaces.
    vec3 a0 = vec3(0.0), a1 = vec3(0.0), a2 = vec3(0.0), a3 = vec3(0.0);
    totalW = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        vec3 w3 = mix(1.0 - frac, frac, vec3(off));
        float w = w3.x * w3.y * w3.z;
        if (w <= 0.0)
            continue;
        ivec3 lc = base + off;
        vec3 probeWorld = vec3(lc) * float(s);

        vec3 toProbe = probeWorld - worldPos;
        float len = length(toProbe);
        if (len > 1e-4)
        {
            // Smooth backface fade (linear in the half-angle term, not squared): still suppresses probes
            // behind the surface to limit leaking, but with gentler position-dependent modulation so flat
            // surfaces don't ripple as the per-probe direction sweeps across them.
            float wn = dot(n, toProbe / len) * 0.5 + 0.5;
            w *= wn;
        }

        // Single-moment occlusion: if the surface point lies beyond the probe's mean free-space radius r,
        // geometry is likely between them, so fade the probe out over the outer half of r. r <= 0 means the
        // probe has no distance data yet -> don't occlude.
        uint cellBase = giProbeBase(c, lc);

        // Reject probes embedded in geometry (mostly-backface gather) — their near-black SH is not signal.
        w *= 1.0 - smoothstep(GI_BACKFACE_DEAD_MIN, GI_BACKFACE_DEAD_MAX, giProbeBackfaceFrac(cellBase));
        if (w <= 0.0)
            continue;

        float r = giProbeMeanDist(cellBase);
        if (r > 1e-3)
            w *= clamp(1.0 - (len - r) / max(r * 0.5, 1e-3), 0.0, 1.0);
        if (w <= 0.0)
            continue;

        vec3 c0, c1, c2, c3;
        giReadSH(cellBase, c0, c1, c2, c3);
        a0 += w * c0; a1 += w * c1; a2 += w * c2; a3 += w * c3;
        totalW += w;
    }
    if (totalW <= 1e-4)
        return vec3(0.0);
    float inv = 1.0 / totalW;
    return giEvalSH(a0 * inv, a1 * inv, a2 * inv, a3 * inv, n);
}

// Trilinear probe irradiance with cross-cascade blending. Walks coarse-to-fine and uses the finest cascade
// that both fits and has front-facing probes; near that cascade's outer window faces it cross-fades into
// the next coarser cascade so the LOD/spacing change is gradual instead of a hard, wavy seam. Returns
// vec3(-1) when no cascade covers the point (caller falls back to ambient).
// Normal-biased sample point for a cascade. The bias grows with the cascade's probe spacing so distant,
// coarse lookups sit cleanly between probes (and off the surface) instead of grazing it and picking up
// contaminated / in-geometry probes.
vec3 giBiasedSample(vec3 worldPos, vec3 n, int s)
{
    return worldPos + n * (GI_NORMAL_BIAS + GI_NORMAL_BIAS_SPACING * float(s));
}

vec3 evalProbeSH(vec3 worldPos, vec3 n)
{
    for (int c = 0; c < GI_NUM_CASCADES; ++c)
    {
        vec3  p = giBiasedSample(worldPos, n, giCascadeSpacing(c));
        ivec3 base, origin; int s; vec3 frac;
        if (!giCascadeFits(c, p, base, origin, s, frac))
            continue;

        float w0;
        vec3 E0 = giSampleCascade(c, s, base, frac, worldPos, n, w0);
        if (w0 <= 1e-4)
            continue; // every probe backfaced -> try a coarser (differently-aligned) cascade

        // Fade toward the next cascade over the outer `band` cells of this window (1 = interior, 0 = face).
        vec3  cellInWin = vec3(base - origin);
        vec3  distCells = min(cellInWin, vec3(GI_CASCADE_PROBE_DIM - 2) - cellInWin);
        float edge = min(min(distCells.x, distCells.y), distCells.z);
        float band = float(GI_CASCADE_PROBE_DIM) * 0.1;
        float fade = clamp(edge / band, 0.0, 1.0);
        if (fade >= 1.0 || c == GI_NUM_CASCADES - 1)
            return E0;

        vec3  p2 = giBiasedSample(worldPos, n, giCascadeSpacing(c + 1));
        ivec3 base2, origin2; int s2; vec3 frac2;
        if (!giCascadeFits(c + 1, p2, base2, origin2, s2, frac2))
            return E0;
        float w1;
        vec3 E1 = giSampleCascade(c + 1, s2, base2, frac2, worldPos, n, w1);
        if (w1 <= 1e-4)
            return E0;
        return mix(E1, E0, fade);
    }
    return vec3(-1.0);
}

// Debug visualization: hue = cascade (red=0 finest .. yellow=coarsest), brightness checkerboarded per probe.
vec3 giDebugColor(vec3 worldPos, vec3 n)
{
    vec3 p = worldPos + n * GI_NORMAL_BIAS;
    for (int c = 0; c < GI_NUM_CASCADES; ++c)
    {
        int   s      = giCascadeSpacing(c);
        ivec3 origin = giCascadeOrigin(c, u_viewPos);
        ivec3 base   = ivec3(floor(p / float(s)));
        if (any(lessThan(base, origin)) || any(greaterThanEqual(base + 1, origin + GI_CASCADE_PROBE_DIM)))
            continue;

        vec3 lod;
        if      (c == 0) lod = vec3(1.0, 0.2, 0.2);
        else if (c == 1) lod = vec3(0.2, 1.0, 0.2);
        else if (c == 2) lod = vec3(0.3, 0.5, 1.0);
        else             lod = vec3(1.0, 1.0, 0.2);
        float checker = (((base.x + base.y + base.z) & 1) == 0) ? 1.0 : 0.55;
        return lod * checker;
    }
    return vec3(0.0);
}

#ifdef GI_PROBE_WRITE

void giStoreCell(uint cellBase, vec3 c0, vec3 c1, vec3 c2, vec3 c3)
{
    GI_GRID_DATA_NAME[cellBase + 0u]  = c0.r;
    GI_GRID_DATA_NAME[cellBase + 1u]  = c0.g;
    GI_GRID_DATA_NAME[cellBase + 2u]  = c0.b;
    GI_GRID_DATA_NAME[cellBase + 3u]  = c1.r;
    GI_GRID_DATA_NAME[cellBase + 4u]  = c1.g;
    GI_GRID_DATA_NAME[cellBase + 5u]  = c1.b;
    GI_GRID_DATA_NAME[cellBase + 6u]  = c2.r;
    GI_GRID_DATA_NAME[cellBase + 7u]  = c2.g;
    GI_GRID_DATA_NAME[cellBase + 8u]  = c2.b;
    GI_GRID_DATA_NAME[cellBase + 9u]  = c3.r;
    GI_GRID_DATA_NAME[cellBase + 10u] = c3.g;
    GI_GRID_DATA_NAME[cellBase + 11u] = c3.b;
}

// Temporally blend this frame's freshly-projected coefficients into a probe (lerp toward the new value).
// alpha == 1 fully replaces the stored value (used for probes that just scrolled into the clipmap).
void giBlendCell(uint cellBase, vec3 c0, vec3 c1, vec3 c2, vec3 c3, float alpha)
{
    for (uint k = 0u; k < 12u; ++k)
    {
        float prev = GI_GRID_DATA_NAME[cellBase + k];
        float next;
        if      (k < 3u)  next = c0[k];
        else if (k < 6u)  next = c1[k - 3u];
        else if (k < 9u)  next = c2[k - 6u];
        else              next = c3[k - 9u];
        GI_GRID_DATA_NAME[cellBase + k] = mix(prev, next, alpha);
    }
}

// Temporally blend the probe's mean free-space distance (the single-moment occlusion estimate) and its
// backface-hit fraction (the embedded-probe rejection signal).
void giBlendProbeStats(uint cellBase, float meanDist, float backfaceFrac, float alpha)
{
    uint  base = cellBase + uint(GI_SH_STRIDE);
    GI_GRID_DATA_NAME[base]      = mix(GI_GRID_DATA_NAME[base],      meanDist,     alpha);
    GI_GRID_DATA_NAME[base + 1u] = mix(GI_GRID_DATA_NAME[base + 1u], backfaceFrac, alpha);
}

#endif // GI_PROBE_WRITE

#endif // GI_PROBE_INC_GLSL
