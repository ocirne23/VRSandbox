#ifndef OCEAN_WAVE_INC_GLSL
#define OCEAN_WAVE_INC_GLSL

// Sampling helpers for the FFT ocean maps (OceanSimulationPipeline / ocean_*.cs.glsl), per cascade c:
//   layer c                      : displacement (Dx, h, Dz, dDx/dz)
//   layer   OCEAN_CASCADES + c   : gradients    (dh/dx, dh/dz, dDx/dx, dDz/dz)
//   layer 2*OCEAN_CASCADES + c   : slope second moments (dhx^2, dhz^2, accel, turbulence [c=0 only])
// Shared by the displacement passes (instanced_indirect_ocean.vs.glsl, gbuffer.vs.glsl) and the water
// shading (ocean.fs.glsl) so prepass depth, drawn geometry and shaded normals read the same field.
// Requires ubo.inc.glsl; includer may set OCEAN_MAPS_BINDING / TERRAIN_HEIGHT_BINDING first.

#ifndef OCEAN_MAPS_BINDING
#define OCEAN_MAPS_BINDING 7
#endif
layout (binding = OCEAN_MAPS_BINDING) uniform sampler2DArray u_oceanMaps;

#ifdef TERRAIN_HEIGHT_BINDING
#include "terrain_height.inc.glsl"
#endif

// (terrain height, water surface level) at worldXZ; open-ocean bottom / sea level without terrain data.
// Depth derives live as level - height; the clipmap lifts its vertices by level - sea level.
vec2 oceanSampleShoreData(vec2 worldXZ)
{
    float height = u_oceanParams2.w - u_oceanParams1.z; // open-ocean bottom: sea level - depth D
    float level = u_oceanParams2.w;                     // sea level
#ifdef TERRAIN_HEIGHT_BINDING
    if (terrainHeightMapPresent())
    {
        const vec4 td = terrainDataAt(worldXZ); // .x = terrain height, .y = water level
        height = td.x;
        level = td.y;
    }
#endif
    return vec2(height, level);
}

// Water depth (m) at worldXZ: local water surface minus terrain.
float oceanSampleShoreDepth(vec2 worldXZ)
{
    const vec2 hw = oceanSampleShoreData(worldXZ);
    return hw.y - hw.x;
}

// Per-texel wave-travel rotation. DISABLED (identity): rotating the sample domain pivots on the world
// origin, so the sea creases along every 8-bit angle contour — the baked flow steers the SIMULATION
// wind instead (OceanGenerator::steeredWindAngle). If ever revived: every wave-sampling pass must
// apply the identical rotation, and the field TRAVELS AGAINST u_oceanParams0.xy.
vec2 oceanFlowRotation(vec2 worldXZ)
{
    return vec2(1.0, 0.0);
}

// Rotate the FFT sampling position by -delta / a resulting horizontal vector back into world space.
vec2 oceanFlowSamplePos(vec2 worldXZ, vec2 fr) { return vec2(fr.x * worldXZ.x + fr.y * worldXZ.y, -fr.y * worldXZ.x + fr.x * worldXZ.y); }
vec2 oceanFlowToWorld(vec2 v, vec2 fr) { return vec2(fr.x * v.x - fr.y * v.y, fr.y * v.x + fr.x * v.y); }

// Land cull: true when the whole triangle footprint is buried under land — the VS then emits a NaN
// position, discarding the primitives. Decided by the vertex's own shoreHW alone (no fetches): safety
// comes from the burial requirement, not sampling density — near-cascade footprints must be buried
// deeper than their own radius (a 45-degree slope bound: smoother terrain can't reach water anywhere a
// co-triangle vertex sits), far/blend-band data uses the flat "Far cull error (m)" (0 = no far cull;
// mistakes out there are sub-pixel). Cliffs steeper than 45 degrees can clip a sliver at their base.
// Both displacement passes MUST apply the same test or their geometry diverges.
bool oceanVertexCulled(vec2 worldXZ, float cellSize, vec2 shoreHW)
{
    const float margin = u_oceanParams7.x;
    if (margin <= 0.0)
        return false;
    const float reach = 3.0 * cellSize; // after the CDLOD morph no co-triangle vertex lies further away

    // Never cull past the streamed terrain mesh (u_terrainParams.x): the cull is only invisible while
    // a rendered mesh stands above the water — the clamp-to-edge-extended bakes report "land" forever.
    if (distance(worldXZ, u_viewPos.xz) + reach >= u_terrainParams.x)
        return false;

    float err = -1.0; // burial slack on top of margin + swash reach
#ifdef TERRAIN_HEIGHT_BINDING
    if (terrainHeightMapPresent())
    {
        const vec2 rel = worldXZ - u_fogParams5.xy;
        const float cheb = max(abs(rel.x), abs(rel.y));
        const float invNear = u_fogParams3.y;
        if ((cheb + reach) * invNear < 0.42) // full-weight near region (blend starts at 0.42)
            err = reach + 0.5 / (float(textureSize(u_terrainHeight, 0).x) * invNear);
        else
        {
            const float invFar = u_fogParams5.z;
            if (u_oceanParams6.x <= 0.0 || invFar <= 0.0 || (cheb + reach) * invFar >= 0.48)
                return false; // far cull off / footprint reaches the far map's clamp-to-edge border
            err = u_oceanParams6.x;
        }
    }
#endif
    if (err < 0.0)
        return false; // no baked terrain data: open-ocean fallback, always water

    // Swash reach (u_oceanParams7.w) keeps the wet band above the waterline alive.
    return shoreHW.y - shoreHW.x < -(margin + u_oceanParams7.w + err);
}

// Fake shoaling: a cascade fades out below depth = "Shoal depth scale" * its patch size — long swell
// dies offshore, chop runs to the beach, everything reaches zero at the waterline.
float oceanShoalFade(float depth, float patchSize)
{
    return smoothstep(0.0, max(u_oceanParams4.z * patchSize, 0.01), depth);
}

// Vertex displacement mip: matches the ring's FIXED cell size (Nyquist band-limit, no camera-coupled
// morphing); `morph` blends +1 across the CDLOD boundary so adjacent rings meet exactly.
// Both displacement passes MUST use this same function.
float oceanVertexLod(float cellSize, float morph, float patchSize)
{
    return max(log2(max(cellSize, 1e-3) * float(OCEAN_FFT_SIZE) / patchSize) + morph + u_oceanParams3.w, 0.0);
}

// Swash weight: how much of the RAW (un-shoaled) wave field rides the surface at this depth (negative
// = land height). Landlocked water (baked level off sea level) gets none — no swell reaches it.
// Used identically by displacement + shading; underwaterLiveWaveY (underwater_light.inc) mirrors it.
float oceanSwashWeight(float depth, float waterLevel)
{
    const float amp = u_oceanParams7.z;
    if (amp <= 0.0)
        return 0.0;
    const float seaFade = 1.0 - smoothstep(0.05, 1.0, abs(waterLevel - u_oceanParams2.w));
    if (seaFade <= 0.0)
        return 0.0;
    const float reach = max(u_oceanParams7.w, 0.01);
    const float landFade = clamp(1.0 + min(depth, 0.0) / reach, 0.0, 1.0);
    const float fadeIn = 1.0 - smoothstep(0.0, max(2.0 * reach, u_oceanParams4.z * u_oceanParams2.y), depth);
    return amp * seaFade * landFade * fadeIn;
}

// Cascade displacement sum at an undisplaced (morphed) world XZ. Choppy lambda applied here so it
// stays live (the maps store raw Dx/Dz). shoreHW = the vertex's (terrain height, water level),
// fetched once by the caller. The CPU buoyancy mirror (OceanGenerator::sampleDisplacement) MUST
// match this function change for change.
vec3 oceanSampleDisplacement(vec2 worldXZ, float cellSize, float morph, vec2 shoreHW)
{
    const float chop = u_oceanParams0.w;
    const float depth = shoreHW.y - shoreHW.x;
    vec3 disp = vec3(0.0);
    float sw = 0.0;
    // Buried deeper than the swash band: all fades are zero — skip the fetches (bit-identical result).
    if (depth > -u_oceanParams7.w)
    {
        const vec2 fr = oceanFlowRotation(worldXZ);
        const vec2 sampleXZ = oceanFlowSamplePos(worldXZ, fr);
        float rawY = 0.0;
        vec2 rawXZ = vec2(0.0);
        for (int c = 0; c < OCEAN_CASCADES; ++c)
        {
            const float L = u_oceanParams2[c];
            const vec4 d = textureLod(u_oceanMaps, vec3(sampleXZ / L, float(c)), oceanVertexLod(cellSize, morph, L));
            disp += vec3(d.x * chop, d.y, d.z * chop) * oceanShoalFade(depth, L);
            rawY += d.y;
            rawXZ += d.xz;
        }
        sw = oceanSwashWeight(depth, shoreHW.y);
        // Swash backflow: the raw chop slides the tongue seaward as the wave recedes. Gated by the
        // tongue's thickness above the sand (a buried surface must not keep sliding), soft-capped to
        // ~the swash reach (the raw offset is unbounded and would shear triangles into streaks).
        const float flowFade = smoothstep(0.0, 0.35, rawY * sw + depth);
        vec2 flowOff = rawXZ * (chop * u_oceanParams8.z * sw * flowFade);
        const float flowCap = clamp(0.5 * u_oceanParams7.w, 0.25, 1.0);
        flowOff *= flowCap / (flowCap + length(flowOff));
        disp.xz += flowOff;
        disp.xz = oceanFlowToWorld(disp.xz, fr);
        // Swash run-up: the RAW wave height rides through the waterline and up the beach; the depth
        // buffer's surface-vs-terrain intersection draws the surging/draining tongue.
        disp.y += rawY * sw;
    }
    // Waterline floor: inner-rounded smooth max holding the surface just above the seabed (no exposed
    // bottom in a trough), ~5 cm below the calm level on land so the DEPTH BUFFER cuts the waterline
    // per pixel (the baked map and the rendered mesh disagree by decimeters — no vertex-level shaping
    // can beat that intersection). k tightens under an active swash so the thin film keeps its ripples.
    const float eps = 0.05;
    const float k = mix(0.2, 0.06, clamp(sw * 4.0, 0.0, 1.0));
    float floorY = eps - max(depth, 2.0 * eps);
    // Trough margin: extra seabed clearance covering the baked-map vs terrain-mesh disagreement,
    // tapered in with depth so the waterline itself doesn't lift and retreat.
    const float troughMargin = u_oceanParams9.x;
    if (troughMargin > 0.0 && depth > 0.0)
        floorY += troughMargin * smoothstep(0.0, 2.0 * troughMargin, depth);
    // Swash drawdown: inside the swash shallows the floor inverts BELOW the seabed so a receding
    // surface sinks under the sand and the waterline genuinely retreats. Gated on the tweak itself:
    // max(x, eps) alone would still bury 5 cm at x = 0 and the setting couldn't be turned off.
    const float swashReach = u_oceanParams7.w;
    if (u_oceanParams7.z > 0.0 && depth > 0.0 && u_oceanParams8.x > 0.0)
        floorY = mix(floorY, -depth - max(u_oceanParams8.x, eps), 1.0 - smoothstep(0.0, max(swashReach, 0.01), depth));
    const float hh = max(k - abs(disp.y - floorY), 0.0) / k;
    disp.y = max(disp.y, floorY) - hh * hh * (k * 0.25);
    return disp;
}

// Combined surface data for the water fragment shader (implicit-LOD: the mip chain prefilters
// distant slopes):
//   slope    : chop-corrected slope (Tessendorf: grad h / (1 + lambda dD))
//   jacobian : horizontal fold J (< ~0.5 = folding crest)
//   slopeVar : LEAN term (Bruneton 2010) — slope variance lost to mip filtering, returned as
//              microfacet roughness (the elongated sun glitter at distance)
//   accel    : vertical acceleration (breaking-crest foam driver)
//   shoreHW  : the (terrain height, water level) fetch, returned for the caller to reuse
void oceanSampleSurface(vec2 worldXZ, out vec2 slope, out float jacobian, out vec2 slopeVar, out float accel, out vec2 shoreHW)
{
    const float chop = u_oceanParams0.w;
    shoreHW = oceanSampleShoreData(worldXZ);
    const float depth = shoreHW.y - shoreHW.x;
    // Buried under land: flat calm surface (with swash on, the run-up band still shades).
    if (depth <= (u_oceanParams7.z > 0.0 ? -u_oceanParams7.w : 0.0))
    {
        slope = vec2(0.0);
        jacobian = 1.0;
        slopeVar = vec2(0.0);
        accel = 0.0;
        return;
    }
    const vec2 fr = oceanFlowRotation(worldXZ);
    const vec2 sampleXZ = oceanFlowSamplePos(worldXZ, fr);
    vec2 slopeSum = vec2(0.0);
    vec2 varSum = vec2(0.0);
    vec2 rawSlope = vec2(0.0);
    vec2 rawVar = vec2(0.0);
    float sxx = 0.0, szz = 0.0, sxz = 0.0;
    float rawAccel = 0.0;
    accel = 0.0;
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const vec2 uv = sampleXZ / u_oceanParams2[c];
        const vec4 g = texture(u_oceanMaps, vec3(uv, float(OCEAN_CASCADES + c)));
        const vec4 d = texture(u_oceanMaps, vec3(uv, float(c)));
        const vec4 m = texture(u_oceanMaps, vec3(uv, float(2 * OCEAN_CASCADES + c)));
        // Same shoal fade as the displacement, so shading tracks the flattened geometry (variance ~ fade^2).
        const float fade = oceanShoalFade(depth, u_oceanParams2[c]);
        slopeSum += g.xy * fade;
        varSum += max(m.xy - g.xy * g.xy, vec2(0.0)) * (fade * fade);
        sxx += g.z * fade; szz += g.w * fade; sxz += d.w * fade;
        accel += m.z * fade;
        rawSlope += g.xy;
        rawVar += max(m.xy - g.xy * g.xy, vec2(0.0));
        rawAccel += m.z;
    }
    // The swash's raw residual must appear in the shading too, or the tongue shades as a mirror sheet.
    const float w = oceanSwashWeight(depth, shoreHW.y);
    if (w > 0.0)
    {
        slopeSum += rawSlope * w;
        varSum += rawVar * (w * w);
        accel += rawAccel * w;
    }
    const float jxx = 1.0 + chop * sxx;
    const float jzz = 1.0 + chop * szz;
    const float jxz = chop * sxz;
    jacobian = jxx * jzz - jxz * jxz;
    // Floor + rational soft limit: near folds the raw division explodes the slope into dark creases.
    slope = slopeSum / max(vec2(jxx, jzz), vec2(0.6));
    slope /= 1.0 + 0.2 * length(slope);
    slope = oceanFlowToWorld(slope, fr);
    slopeVar = varSum;
}

// Instant crest foam from the fold Jacobian + downward acceleration (Longuet-Higgins). ONE function
// shared by the water shader (display, biasBoost = turbulence * "Foam boost") and ocean_foam.cs.glsl
// (injection, biasBoost = 0 — only genuine breaking adds energy, no feedback loop).
float oceanInstantFoam(float jacobian, float accel, float biasBoost)
{
    const float softness = max(u_oceanParams4.w, 0.02);
    const float bias = u_oceanFoam.w + biasBoost;
    const float fold = 1.0 - smoothstep(bias - softness, bias, jacobian);
    const float breaking = smoothstep(u_oceanParams5.w, u_oceanParams5.w + softness, -accel / 9.81);
    return max(fold, breaking);
}

// Accumulated turbulence (moments[c0].w): the decaying memory of breaking. Magnified hard near the
// camera, so mip 0 gets a 3x3 tent of bilinear taps (reconstruction AA); crossfades to plain
// trilinear once the pixel footprint reaches mip 1. footprint = world size of one pixel.
float oceanSampleTurbulence(vec2 worldXZ, float footprint)
{
    const float L0 = u_oceanParams2.x;
    const float layer = float(2 * OCEAN_CASCADES);
    const float size = float(OCEAN_FFT_SIZE);

    const float lod = log2(max(footprint * size / L0, 1e-3));
    const float trilinear = texture(u_oceanMaps, vec3(worldXZ / L0, layer)).w;
    if (lod >= 1.0)
        return trilinear;

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

// Vertex-shader shading normal with explicit LOD (the G-buffer prepass writes this as the reference
// normal). shoreHW = the vertex's (terrain height, water level), fetched once by the caller.
vec3 oceanSampleNormalLod(vec2 worldXZ, float cellSize, float morph, vec2 shoreHW)
{
    const float chop = u_oceanParams0.w;
    const float depth = shoreHW.y - shoreHW.x;
    if (depth <= 0.0)
        return vec3(0.0, 1.0, 0.0); // buried under land: flat
    const vec2 fr = oceanFlowRotation(worldXZ);
    const vec2 sampleXZ = oceanFlowSamplePos(worldXZ, fr);
    vec2 slopeSum = vec2(0.0);
    vec2 sxx_szz = vec2(0.0);
    for (int c = 0; c < OCEAN_CASCADES; ++c)
    {
        const float L = u_oceanParams2[c];
        const vec4 g = textureLod(u_oceanMaps, vec3(sampleXZ / L, float(OCEAN_CASCADES + c)), oceanVertexLod(cellSize, morph, L));
        const float fade = oceanShoalFade(depth, L);
        slopeSum += g.xy * fade;
        sxx_szz += g.zw * fade;
    }
    vec2 slope = slopeSum / max(vec2(1.0) + chop * sxx_szz, vec2(0.6));
    slope /= 1.0 + 0.2 * length(slope); // same fold-over soft limit as oceanSampleSurface
    slope = oceanFlowToWorld(slope, fr);
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

#endif
