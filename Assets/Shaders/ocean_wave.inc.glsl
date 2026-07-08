#ifndef OCEAN_WAVE_INC_GLSL
#define OCEAN_WAVE_INC_GLSL

// Sampling helpers for the FFT ocean maps produced by OceanSimulationPipeline (ocean_*.cs.glsl):
// a 2D array texture with, per cascade c:
//   layer c                      : displacement (Dx, h, Dz, dDx/dz)
//   layer   OCEAN_CASCADES + c   : gradients    (dh/dx, dh/dz, dDx/dx, dDz/dz)
//   layer 2*OCEAN_CASCADES + c   : slope second moments (dhx^2, dhz^2, dhx*dhz, foam [c=0 only])
// all with a full mip chain. Shared by the raster displacement passes (instanced_indirect.vs.glsl with
// OCEAN, the G-buffer prepass gbuffer.vs.glsl) and the water shading (ocean.fs.glsl), so the prepass
// depth, the drawn geometry and the shaded normals all read the exact same wave field. Requires
// ubo.inc.glsl. The includer may set OCEAN_MAPS_BINDING before including (the G-buffer pipeline binds
// the maps at a different slot than the forward pipeline).

#ifndef OCEAN_MAPS_BINDING
#define OCEAN_MAPS_BINDING 7
#endif
layout (binding = OCEAN_MAPS_BINDING) uniform sampler2DArray u_oceanMaps;

// Explicit LOD for vertex-shader displacement sampling: pick the mip whose texel matches the LOCAL GRID
// CELL SIZE (u_oceanParams3.xy: cell = A*dist + B, the analytic spacing of OceanRenderer's graded grid).
// This band-limits the displacement to what the mesh can represent (Nyquist) — point-sampling finer
// content aliases the surface, which reads as waves snapping around as the grid follows the camera.
// Both displacement passes (prepass + forward) MUST use this same function so their positions match.
float oceanVertexLod(float dist, float patchSize)
{
    const float cellSize = u_oceanParams3.x * dist + u_oceanParams3.y;
    return max(log2(max(cellSize, 1e-3) * float(OCEAN_FFT_SIZE) / patchSize), 0.0);
}

// Sum of all cascades' displacement at an undisplaced world XZ. Choppy lambda (u_oceanParams0.w) is
// applied here so it stays live (the maps store raw Tessendorf Dx/Dz).
vec3 oceanSampleDisplacement(vec2 worldXZ, float dist)
{
    const float chop = u_oceanParams0.w;
    vec3 disp = vec3(0.0);
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const float L = u_oceanParams2[c];
        const vec4 d = textureLod(u_oceanMaps, vec3(worldXZ / L, float(c)), oceanVertexLod(dist, L));
        disp += vec3(d.x * chop, d.y, d.z * chop);
    }
    return disp;
}

// Combined surface data for the water fragment shader (implicit-LOD samples: the mip chain prefilters
// distant slopes, so the far sea reads smooth instead of shimmering):
//   slope    : chop-corrected surface slope (Tessendorf: eps = grad h / (1 + lambda dD))
//   jacobian : horizontal-displacement fold J = (1+l dDxx)(1+l dDzz) - (l dDxz)^2 (< ~0.5 = folding crest)
//   slopeVar : filtered-out slope variance per axis, E[s^2] - E[s]^2 summed over cascades — the LEAN /
//              Bruneton 2010 term: sub-texel wave detail lost to mip filtering comes back as microfacet
//              roughness, which is what produces the elongated glittering sun path at distance.
//   accel    : the surface's vertical acceleration (m/s^2, negative = downward) — with the Jacobian this
//              drives the instantaneous, geometry-locked crest foam in the water shader
void oceanSampleSurface(vec2 worldXZ, out vec2 slope, out float jacobian, out vec2 slopeVar, out float accel)
{
    const float chop = u_oceanParams0.w;
    vec2 slopeSum = vec2(0.0);
    vec2 varSum = vec2(0.0);
    float sxx = 0.0, szz = 0.0, sxz = 0.0;
    accel = 0.0;
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const vec2 uv = worldXZ / u_oceanParams2[c];
        const vec4 g = texture(u_oceanMaps, vec3(uv, float(OCEAN_CASCADES + c)));
        const vec4 d = texture(u_oceanMaps, vec3(uv, float(c)));
        const vec4 m = texture(u_oceanMaps, vec3(uv, float(2 * OCEAN_CASCADES + c)));
        slopeSum += g.xy;
        varSum += max(m.xy - g.xy * g.xy, vec2(0.0)); // E[s^2] - E[s]^2 (0 at mip 0 by construction)
        sxx += g.z; szz += g.w; sxz += d.w;
        accel += m.z;
    }
    const float jxx = 1.0 + chop * sxx;
    const float jzz = 1.0 + chop * szz;
    const float jxz = chop * sxz;
    jacobian = jxx * jzz - jxz * jxz;
    // Chop-corrected slope (Tessendorf), with a GENTLE floor and a rational soft limit on the magnitude:
    // near folds the (1 + lambda dD) terms approach zero and the raw division explodes the slope, which
    // tips the normal past grazing and shades as harsh dark creases along every crest.
    slope = slopeSum / max(vec2(jxx, jzz), vec2(0.6));
    slope /= 1.0 + 0.2 * length(slope);
    slopeVar = varSum;
}

// Instantaneous crest foam from the surface's fold Jacobian (Tessendorf) and downward vertical
// acceleration (Longuet-Higgins breaking criterion). ONE function shared by the water shader (display)
// and ocean_foam.cs.glsl (turbulence injection) — same thresholds ("Fold bias", "Break accel"), same
// edge ("Softness"). biasBoost relaxes the fold threshold: the water shader passes the accumulated
// TURBULENCE scaled by "Foam boost", so energized water foams on ever-milder convergence of the live
// geometry (dense textured foam in a fresh wake, thinning to streaks along real fold lines as the
// turbulence decays). The injection passes 0 — only genuine breaking adds energy (no feedback loop).
float oceanInstantFoam(float jacobian, float accel, float biasBoost)
{
    const float softness = max(u_oceanParams4.w, 0.02);
    const float bias = u_oceanFoam.w + biasBoost;
    const float fold = 1.0 - smoothstep(bias - softness, bias, jacobian);
    const float breaking = smoothstep(u_oceanParams5.w, u_oceanParams5.w + softness, -accel / 9.81);
    return max(fold, breaking);
}

// Temporally accumulated TURBULENCE field (ocean_foam.cs.glsl, stored in moments[c0].w): the decaying
// memory of breaking events. The water shader turns it into aged foam (fold-threshold relaxation via
// oceanInstantFoam's biasBoost), milky entrained-bubble water and extra surface roughness. The field is
// coarse by design and magnified a LOT near the camera — plain bilinear shows its texels as
// diamond-shaped gradients, so it is multi-sampled: a 3x3 tent of bilinear taps at 1-texel spacing
// (fixed reconstruction AA; the real texel-structure removal is the accumulation's own diffusion —
// "Ocean/Foam/Spread"). Crossfaded to the plain trilinear sample once the pixel footprint reaches
// mip 1 (minified: the mip chain already filters, and magnification artifacts cannot show).
// footprint = world size of one pixel.
float oceanSampleTurbulence(vec2 worldXZ, float footprint)
{
    const float L0 = u_oceanParams2.x;
    const float layer = float(2 * OCEAN_CASCADES); // cascade 0 moments (w = coverage)
    const float size = float(OCEAN_FFT_SIZE);

    // Screen-matched mip of this map; >= 1 means minification, where trilinear is correct and cheap.
    const float lod = log2(max(footprint * size / L0, 1e-3));
    const float trilinear = texture(u_oceanMaps, vec3(worldXZ / L0, layer)).w;
    if (lod >= 1.0)
        return trilinear;

    // 3x3 tent (weights 1-2-1 outer product / 16) of bilinear mip-0 taps.
    const vec2 uv = worldXZ / L0;
    const vec2 spacing = vec2(1.0 / size);
    float filtered = 0.0;
    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            const float w = float((2 - abs(i)) * (2 - abs(j)));
            filtered += textureLod(u_oceanMaps, vec3(uv + vec2(float(i), float(j)) * spacing, layer), 0.0).w * w;
        }
    }
    filtered *= 1.0 / 16.0;

    return mix(filtered, trilinear, clamp(lod, 0.0, 1.0));
}

// Vertex-shader variant with explicit LOD (the G-buffer prepass writes this as the reference normal).
vec3 oceanSampleNormalLod(vec2 worldXZ, float dist)
{
    const float chop = u_oceanParams0.w;
    vec2 slopeSum = vec2(0.0);
    vec2 sxx_szz = vec2(0.0);
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const float L = u_oceanParams2[c];
        const vec4 g = textureLod(u_oceanMaps, vec3(worldXZ / L, float(OCEAN_CASCADES + c)), oceanVertexLod(dist, L));
        slopeSum += g.xy;
        sxx_szz += g.zw;
    }
    vec2 slope = slopeSum / max(vec2(1.0) + chop * sxx_szz, vec2(0.6));
    slope /= 1.0 + 0.2 * length(slope); // same fold-over soft limit as oceanSampleSurface
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

#endif
