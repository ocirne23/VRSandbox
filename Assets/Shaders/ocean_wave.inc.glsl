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

// Shore water-depth map (optional; the includer defines OCEAN_SHORE_BINDING to enable it): an R32F
// snapshot of the terrain height field around the camera, baked on the CPU (meters of water below the
// calm sea surface; negative = land above sea level). Drives fake shoaling + the shoreline surf band.
#ifdef OCEAN_SHORE_BINDING
layout (binding = OCEAN_SHORE_BINDING) uniform sampler2D u_oceanShore;
#endif

// Water depth (m) at worldXZ. Outside the baked region — or without a map (u_oceanParams4.x = 0) — this
// returns the open-ocean depth D, blended over the map's outer 5% so leaving the region never pops.
float oceanSampleShoreDepth(vec2 worldXZ)
{
#ifdef OCEAN_SHORE_BINDING
    const float invRange = u_oceanParams4.x;
    if (invRange > 0.0)
    {
        const vec2 uv = (worldXZ - u_oceanParams3.xy) * invRange + 0.5;
        const vec2 e = min(uv, vec2(1.0) - uv);
        const float inMap = clamp(min(e.x, e.y) * 20.0, 0.0, 1.0);
        if (inMap > 0.0)
            return mix(u_oceanParams1.z, textureLod(u_oceanShore, uv, 0.0).x, inMap);
    }
#endif
    return u_oceanParams1.z;
}

// Fake shoaling: a cascade's waves fade out once the water is shallower than a fraction of its patch
// size ("Ocean/Shore/Shoal depth scale") — long swell feels the bottom far offshore while short chop
// runs almost to the beach, and every cascade reaches zero at the waterline so waves never poke through
// land. Real shoaling steepens before breaking; the fade is the stable, artifact-free half of that.
float oceanShoalFade(float depth, float patchSize)
{
    return smoothstep(0.0, max(u_oceanParams4.z * patchSize, 0.01), depth);
}

// Explicit LOD for vertex-shader displacement sampling: pick the mip whose texel matches the vertex's
// CLIPMAP RING cell size (baked per vertex; OceanRenderer's rings have FIXED cell sizes, so every world
// position samples a FIXED mip regardless of camera distance — no wave morphing coupled to camera
// motion). This band-limits the displacement to what the local mesh density can represent (Nyquist) —
// point-sampling finer content aliases the surface. `morph` blends toward the next ring's mip (+1) over
// each ring's outer band (the CDLOD boundary morph), so adjacent rings meet exactly. "Ocean/Detail bias"
// (u_oceanParams3.w) offsets the mip (negative = finer than Nyquist: slight shimmer while moving).
// Both displacement passes (prepass + forward) MUST use this same function so their positions match.
float oceanVertexLod(float cellSize, float morph, float patchSize)
{
    return max(log2(max(cellSize, 1e-3) * float(OCEAN_FFT_SIZE) / patchSize) + morph + u_oceanParams3.w, 0.0);
}

// Sum of all cascades' displacement at an undisplaced (morphed) world XZ. Choppy lambda (u_oceanParams0.w)
// is applied here so it stays live (the maps store raw Tessendorf Dx/Dz).
vec3 oceanSampleDisplacement(vec2 worldXZ, float cellSize, float morph)
{
    const float chop = u_oceanParams0.w;
    const float depth = oceanSampleShoreDepth(worldXZ);
    vec3 disp = vec3(0.0);
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const float L = u_oceanParams2[c];
        const vec4 d = textureLod(u_oceanMaps, vec3(worldXZ / L, float(c)), oceanVertexLod(cellSize, morph, L));
        disp += vec3(d.x * chop, d.y, d.z * chop) * oceanShoalFade(depth, L);
    }
    // Waterline treatment: smooth-clamp the surface to just above the seabed — the shoal fade only
    // scales the waves, so a trough could still swing under the bottom and open a "hole" of exposed
    // seabed. The clamp is an INNER-rounded smooth max (dips at most k/4 below the hard max, never
    // above — no lip of water riding the waterline; k/4 < eps keeps it off the seabed), and the floor
    // is capped ~eps BELOW sea level on land, so over the beach the surface just sinks slightly under
    // the calm line and the DEPTH BUFFER cuts it against the real terrain mesh. That per-pixel
    // intersection IS the waterline: no burying/offset scheme can beat it, because the baked depth map
    // and the rendered terrain triangles disagree by decimeters (bilinear texels vs LOD'd mesh) and any
    // vertex-level shaping against the map pokes out of (or gaps under) the real ground wherever they
    // differ. Deep water (large depth) leaves both terms inert.
    const float eps = 0.05, k = 0.2;
    const float floorY = eps - max(depth, 2.0 * eps);
    const float hh = max(k - abs(disp.y - floorY), 0.0) / k;
    disp.y = max(disp.y, floorY) - hh * hh * (k * 0.25);
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
    const float depth = oceanSampleShoreDepth(worldXZ);
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
        // Shoaling: fade this cascade's contribution with the SAME factor the displacement uses, so the
        // shading (slopes/folds/variance) tracks the flattened shallow-water geometry (variance ~ fade^2).
        const float fade = oceanShoalFade(depth, u_oceanParams2[c]);
        slopeSum += g.xy * fade;
        varSum += max(m.xy - g.xy * g.xy, vec2(0.0)) * (fade * fade);
        sxx += g.z * fade; szz += g.w * fade; sxz += d.w * fade;
        accel += m.z * fade;
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
vec3 oceanSampleNormalLod(vec2 worldXZ, float cellSize, float morph)
{
    const float chop = u_oceanParams0.w;
    const float depth = oceanSampleShoreDepth(worldXZ);
    vec2 slopeSum = vec2(0.0);
    vec2 sxx_szz = vec2(0.0);
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const float L = u_oceanParams2[c];
        const vec4 g = textureLod(u_oceanMaps, vec3(worldXZ / L, float(OCEAN_CASCADES + c)), oceanVertexLod(cellSize, morph, L));
        const float fade = oceanShoalFade(depth, L);
        slopeSum += g.xy * fade;
        sxx_szz += g.zw * fade;
    }
    vec2 slope = slopeSum / max(vec2(1.0) + chop * sxx_szz, vec2(0.6));
    slope /= 1.0 + 0.2 * length(slope); // same fold-over soft limit as oceanSampleSurface
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

#endif
