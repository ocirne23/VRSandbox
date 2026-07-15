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

// Shore map (optional; the includer defines OCEAN_SHORE_BINDING to enable it): an RGBA32F snapshot around
// the camera, CPU-baked. R = raw terrain surface height (world Y, m); G = the WATER SURFACE LEVEL — sea
// level over the open ocean, higher where rivers/lakes sit at altitude; B = 8-bit water flow direction
// (0 = none, 1..255 = angle/2pi; nearest-fetched, drives the wave-travel rotation: toward land through
// the surf zone, easing back to the wind heading offshore — plus generator-authored river directions.
// See HeightMapBaker's applyFlowField); A = spare. The
// water depth is derived live as water level - height; it drives fake shoaling + the shoreline surf band,
// and the clipmap lifts its vertices by water level - sea level. When the includer also defines
// TERRAIN_HEIGHT_BINDING, the coarser fog terrain cascades (terrain_height.inc.glsl — the same terrain
// height AND water level over a much larger range) supply both beyond this map's reach, so distant
// coastlines still shoal, far lakes keep their level, and waves never poke through far-away land.
#ifdef OCEAN_SHORE_BINDING
layout (binding = OCEAN_SHORE_BINDING) uniform sampler2D u_shoreHeight;
#endif
#ifdef TERRAIN_HEIGHT_BINDING
#include "terrain_height.inc.glsl"
#endif

// (terrain height, water surface level) at worldXZ. Outside the shore map — or without one
// (u_oceanParams4.x = 0) — terrain falls back to the fog cascades then the open-ocean bottom, and the
// water level to sea level; each handover blends over an edge band so leaving a map's region never pops.
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
#ifdef OCEAN_SHORE_BINDING
    const float invRange = u_oceanParams4.x;
    if (invRange > 0.0)
    {
        const vec2 uv = (worldXZ - u_oceanParams3.xy) * invRange + 0.5;
        const vec2 e = min(uv, vec2(1.0) - uv);
        const float inMap = clamp(min(e.x, e.y) * 20.0, 0.0, 1.0);
        if (inMap > 0.0)
        {
            const vec2 hw = textureLod(u_shoreHeight, uv, 0.0).xy;
            height = mix(height, hw.x, inMap);
            level  = mix(level, hw.y, inMap);
        }
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

// Vertical lift raising the sea-level clipmap onto the local water table (0 over the open ocean). Both
// displacement passes (prepass + forward) MUST apply this identically, before sampling the displacement.
float oceanWaterOffset(vec2 worldXZ)
{
    return oceanSampleShoreData(worldXZ).y - u_oceanParams2.w;
}

// Local wave-travel basis: the shore map bakes a FLOW DIRECTION (channel B, 8 bits: 0 = none, 1..255 =
// angle/2pi — toward land through the surf zone, authored river directions) that replaces the global
// wind locally. Returns (cos, sin) of the flow-vs-wind rotation.
//
// DISABLED (returns identity): rotating the SAMPLE DOMAIN does not work. The rotation pivots on the
// world origin, so two texels one 8-bit angle step apart (2pi/254) sample the wave field
// |worldXZ| * 0.025 m apart — the sea creases along every quantization contour, worse with distance
// from the origin. The baked flow steers the SIMULATION's wind instead (OceanGenerator's
// steeredWindAngle): the spectrum regenerates every frame, so turning its wind makes the whole field
// genuinely travel toward the local shore, continuous by construction. Kept for a future per-pixel
// take — that needs a formulation whose error does not grow with |worldXZ| (blending pre-rotated
// fields, a local pivot per wave patch, ...). If re-enabled: nearest-texel only (packed angles wrap),
// every pass sampling the wave field MUST apply the same rotation or the prepass depth and the drawn
// surface diverge, and mind the wind convention — the field TRAVELS AGAINST u_oceanParams0.xy (the
// spectrum's dominant term is h0(k) e^{i(k.x + wt)}), so the delta below needs flowAngle vs the
// TRAVEL heading (windAngle + pi), not the wind vector's own angle.
#define OCEAN_FLOW_SAMPLE_ROTATION 0
vec2 oceanFlowRotation(vec2 worldXZ)
{
#if OCEAN_FLOW_SAMPLE_ROTATION
#ifdef OCEAN_SHORE_BINDING
    const float invRange = u_oceanParams4.x;
    if (invRange > 0.0)
    {
        const vec2 uv = (worldXZ - u_oceanParams3.xy) * invRange + 0.5;
        if (all(greaterThan(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0))))
        {
            const ivec2 res = textureSize(u_shoreHeight, 0);
            const uint enc = uint(texelFetch(u_shoreHeight, clamp(ivec2(uv * vec2(res)), ivec2(0), res - 1), 0).z + 0.5);
            if (enc > 0u)
            {
                const float flowAngle = (float(enc) - 1.0) * (6.28318530718 / 254.0);
                const float windAngle = atan(u_oceanParams0.y, u_oceanParams0.x);
                const float d = flowAngle - windAngle;
                return vec2(cos(d), sin(d));
            }
        }
    }
#endif
#endif
    return vec2(1.0, 0.0);
}

// Rotate the FFT sampling position by -delta (waves then TRAVEL along the flow) / a resulting horizontal
// vector back by +delta into world space.
vec2 oceanFlowSamplePos(vec2 worldXZ, vec2 fr) { return vec2(fr.x * worldXZ.x + fr.y * worldXZ.y, -fr.y * worldXZ.x + fr.x * worldXZ.y); }
vec2 oceanFlowToWorld(vec2 v, vec2 fr) { return vec2(fr.x * v.x - fr.y * v.y, fr.y * v.x + fr.x * v.y); }

// Conservative land cull for the clipmap vertex shaders: true when the terrain sits at least
// "Ocean/Shore/Cull margin" (u_oceanParams7.x) above the LOCAL water level across the vertex's whole
// triangle footprint (+-3 cells: after the CDLOD morph no co-triangle vertex lies further away), so
// every primitive using this vertex is fully buried under land — the VS then emits a NaN position,
// which discards those primitives before rasterization. Waves shoal-fade to zero at depth <= 0, so a
// footprint buried by a positive margin can never clip live water; the margin absorbs the decimeter
// disagreement between the baked maps and the LOD'd terrain mesh.
// The taps read oceanSampleShoreDepth — the EXACT field the waves shoal/lift by — but only where the
// data is trustworthy enough to delete geometry on: the shore map close in, then the fog NEAR cascade
// out to its far handover (~860 m). The far cascade never culls: its coarse texels cannot resolve
// narrow water (rivers) and the far terrain LODs stray meters from the bake — distant buried vertices
// keep the depth <= 0 sampling early-outs instead. Tap spacing is capped at 1.5 texels of the map
// covering the footprint (adaptive grid up to 5x5), the burial requirement grows with its texel size,
// and footprints straddling the shore map's edge-blend band (mixed fields) are never culled.
// Both displacement passes (prepass + forward) MUST apply the same test or their geometry diverges.
bool oceanVertexCulled(vec2 worldXZ, float cellSize)
{
    const float margin = u_oceanParams7.x;
    if (margin <= 0.0)
        return false;
    const float reach = 3.0 * cellSize;

    // The cull is only invisible while the RENDERED terrain mesh stands above the water and depth-cuts
    // it. Past the streamed chunks there is no mesh at all — while the baked cascades still report
    // "land" out there (clamp-to-edge extends them forever). Culling there would punch holes showing
    // the underwater fog behind the surface, so never cull once the footprint reaches the streamer's
    // mesh coverage radius (u_terrainParams.x, radial from the camera; 0 = no terrain mesh up).
    if (distance(worldXZ, u_viewPos.xz) + reach >= u_terrainParams.x)
        return false;

    // Which map the footprint reads: its texel size gates the tap density and scales the burial margin.
    float texel = -1.0;
#ifdef OCEAN_SHORE_BINDING
    const float invShore = u_oceanParams4.x;
    if (invShore > 0.0)
    {
        const vec2 uvS = (worldXZ - u_oceanParams3.xy) * invShore + 0.5;
        const vec2 eS = min(uvS, vec2(1.0) - uvS);
        const float e = min(eS.x, eS.y);
        const float r = reach * invShore;
        if (e - r >= 0.06) // fully inside the full-weight interior (inMap blend band ends at 0.05)
            texel = 1.0 / (float(textureSize(u_shoreHeight, 0).x) * invShore);
        else if (e + r > 0.0) // footprint touches the map's influence: mixed fields, don't cull
            return false;
    }
#endif
#ifdef TERRAIN_HEIGHT_BINDING
    if (texel < 0.0 && terrainHeightMapPresent())
    {
        // Only the NEAR fog cascade may cull. The far cascade's coarse texels cannot reliably resolve
        // narrow water — a river renders from it as dashed segments that coarse taps then straddle —
        // and the far terrain mesh LODs deviate from the bake by meters; every far-range cull artifact
        // (horizon holes, rebake flicker, river cuts) traced back to trusting far data. Distant buried
        // geometry keeps the depth <= 0 sampling early-outs; the depth buffer handles the rest.
        const vec2 rel = worldXZ - u_fogParams5.xy;
        const float cheb = max(abs(rel.x), abs(rel.y));
        const float invNear = u_fogParams3.y;
        if ((cheb + reach) * invNear >= 0.42) // footprint touches far-blended data / leaves the near region
            return false;
        texel = 1.0 / (float(textureSize(u_terrainHeight, 0).x) * invNear);
    }
#endif
    if (texel <= 0.0)
        return false; // no baked terrain data here: open-ocean fallback, always water

    // Everything that can make "the map says buried" wrong grows with the map's coarseness: sub-tap
    // water detail the taps can straddle, AND how far the rendered terrain mesh (coarser LODs at
    // distance) deviates from the baked field. So the burial requirement scales with the texel size
    // — near the camera ~the raw margin, ~9 m out at the far cascade: marginal far coastlines never
    // cull (their holes read as underwater-fog haze), plainly inland terrain still does.
    // Swash run-up (u_oceanParams7.w, CPU-estimated max reach) keeps the wet band above the waterline
    // alive: those vertices sit on land but can still rise above the terrain with an incoming wave.
    const float need = margin + u_oceanParams7.w + 0.5 * texel;

    // Taps per axis: enough to keep the spacing under 1.5 texels of the sampled map, at most 5x5
    // (only already-buried vertices ever pay for the grid; the first tap exits over water).
    const float limit = 1.5 * texel;
    const int n = max(1 + int(ceil(2.0 * reach / limit)), 2);
    if (n > 5)
        return false;

    if (oceanSampleShoreDepth(worldXZ) >= -need)
        return false; // water in reach: done (the common case over open water — one tap and out)
    const float spacing = 2.0 * reach / float(n - 1);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            if (oceanSampleShoreDepth(worldXZ + vec2(float(i), float(j)) * spacing - vec2(reach)) >= -need)
                return false;
    return true;
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
// CLIPMAP RING cell size (baked per vertex; OceanGenerator's rings have FIXED cell sizes, so every world
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

// Swash weight: how much of the RAW (un-shoaled) wave field rides the surface at this water depth
// (negative = land height). Three factors: "Swash amplitude", a land-height decay reaching zero at the
// sampling band's edge (no wall of water at the cutoff), and a fade-in over ~2 swash reaches of
// approach depth — wide enough that the residual builds gradually under the dying shoaled waves
// instead of spiking right at the shoreline (the mid-cascade shoal band is the floor so a tiny reach
// never fades sharper than the waves themselves). Used identically by the displacement and the
// shading-surface sample; underwaterLiveWaveY (underwater_light.inc.glsl) mirrors it.
float oceanSwashWeight(float depth, float waterLevel)
{
    const float amp = u_oceanParams7.z;
    if (amp <= 0.0)
        return 0.0;
    // Swash is OPEN-OCEAN swell energy running ashore. Water whose baked level sits off sea level
    // (lakes/ponds/rivers at altitude) is landlocked — no swell reaches it — so the swash fades out
    // over the first meter of elevation difference and their shores keep the plain shoaled waterline.
    const float seaFade = 1.0 - smoothstep(0.05, 1.0, abs(waterLevel - u_oceanParams2.w));
    if (seaFade <= 0.0)
        return 0.0;
    const float reach = max(u_oceanParams7.w, 0.01);
    const float landFade = clamp(1.0 + min(depth, 0.0) / reach, 0.0, 1.0);
    const float fadeIn = 1.0 - smoothstep(0.0, max(2.0 * reach, u_oceanParams4.z * u_oceanParams2.y), depth);
    return amp * seaFade * landFade * fadeIn;
}

// Sum of all cascades' displacement at an undisplaced (morphed) world XZ. Choppy lambda (u_oceanParams0.w)
// is applied here so it stays live (the maps store raw Tessendorf Dx/Dz).
vec3 oceanSampleDisplacement(vec2 worldXZ, float cellSize, float morph)
{
    const float chop = u_oceanParams0.w;
    const vec2 shoreHW = oceanSampleShoreData(worldXZ); // (terrain height, water level)
    const float depth = shoreHW.y - shoreHW.x;
    vec3 disp = vec3(0.0);
    float sw = 0.0;
    // Buried under land (depth <= 0): every cascade's shoal fade is exactly zero, so the map/flow
    // sampling below degenerates to displacing nothing — skip it and let the waterline clamp place
    // the vertex (bit-identical to the full path, just without the texture fetches). The swash band
    // (u_oceanParams7.w, land up to the estimated run-up reach above the water level) still samples:
    // its waves come from the un-shoaled field below.
    if (depth > -u_oceanParams7.w)
    {
        const vec2 fr = oceanFlowRotation(worldXZ);           // rivers: waves travel along the local flow
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
        // Swash backflow ("Swash backflow"): the raw horizontal chop oscillates shoreward under a crest
        // and seaward under a trough — riding it on the swash weight makes the tongue FLOW back down the
        // beach as the wave recedes (the waterline contour slides seaward) instead of sinking in place.
        // Gated by the tongue's THICKNESS above the sand (surface rel. level = rawY*sw, terrain rel.
        // level = -depth): a buried surface must not keep sliding, or the next wave lifts it back out
        // displaced from where it sank — a seam of water emerging from the terrain ahead of the crest.
        const float flowFade = smoothstep(0.0, 0.35, rawY * sw + depth);
        vec2 flowOff = rawXZ * (chop * u_oceanParams8.z * sw * flowFade);
        // Soft-cap the slide to ~the swash reach: the raw chop x backflow is unbounded (metres in a big
        // sea), and a per-vertex offset that large shears the clipmap triangles into streaks that creep
        // up the beach. The rational cap keeps the direction and the small-offset response, and
        // asymptotes to the cap instead of clipping.
        const float flowCap = clamp(0.5 * u_oceanParams7.w, 0.25, 1.0);
        flowOff *= flowCap / (flowCap + length(flowOff));
        disp.xz += flowOff;
        disp.xz = oceanFlowToWorld(disp.xz, fr);
        // Swash run-up ("Ocean/Shore/Swash amplitude"): the shoal fade kills the displayed waves at the
        // waterline, which is what left a static cutoff there. Ride the RAW (un-shoaled) wave height
        // through the waterline and up the beach instead: wherever it beats the terrain the surface
        // pokes above the ground and the depth buffer draws the tongue — the same per-pixel
        // surface-vs-terrain intersection that already IS the waterline, so the wet edge surges up the
        // beach and drains back with each swell. Fades in as the shoal fade takes over (mid cascade as
        // the reference), so deep water sees no double displacement.
        // Crest ceiling: a wave cannot stand taller than the water it is in. The waterline clamp below
        // pins the TROUGH above the seabed, but nothing bounded the crest — the shoal fade is a fixed
        // depth/(scale*patch) ramp, not a limit relative to the water column, so a metre-high crest was
        // legal in two metres of water. It is not the crest itself that shows: it is the clipmap triangle
        // bridging that vertex to the next one on LAND, which interpolates the crest straight over the
        // beach. Real waves break near H/d ~ 0.78 (crest ~ 0.39*d), so bounding the crest to a fraction of
        // depth is what the water would do anyway.
        // BEFORE the swash add, never after: running up the beach is precisely the swash's job, and it
        // rides the raw un-shoaled field on purpose.
        const float crestLimit = u_oceanParams8.w;
        if (crestLimit > 0.0 && depth > 0.0)
            disp.y = min(disp.y, crestLimit * depth);
        disp.y += rawY * sw;
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
    // (The ~3-5 cm land-side offset is enough at ANY distance now that the main view is reversed-Z; a
    // cell-scaled extra burial used to live here for standard-Z depth noise, but its depth ramp exceeded
    // the open-ocean depth at the coarse outer rings and visibly sank the far sea surface.)
    // The smooth-max rounding width k flattens all wave detail within ~k of the floor — fine offshore,
    // but the swash's thin film LIVES in that band: it clamped every centimeter ripple off the receding
    // sheet, leaving a geometrically flat plate. Tighten the rounding toward a near-hard max where the
    // swash is active so the film keeps its fine-cascade texture down to the sand.
    const float eps = 0.05;
    const float k = mix(0.2, 0.06, clamp(sw * 4.0, 0.0, 1.0));
    float floorY = eps - max(depth, 2.0 * eps);
    // Trough margin: hold the surface further above the seabed than the 5 cm above. That eps is measured
    // against the BAKED map, and the terrain MESH you see disagrees with it by decimetres (bilinear texels
    // vs LOD'd triangles) — so a trough 5 cm clear of the map's seabed still sinks under the real ground,
    // which then pokes through the water. The margin has to cover that disagreement, not the wave.
    // TAPERED IN over 2x itself, for two reasons that are really the same one: the margin cannot exceed the
    // water it is lifting the surface inside of, and applying it at the waterline would raise the floor
    // there and march the water's edge seaward, trading a poke-through for a retreated sea.
    const float troughMargin = u_oceanParams9.x;
    if (troughMargin > 0.0 && depth > 0.0)
        floorY += troughMargin * smoothstep(0.0, 2.0 * troughMargin, depth);
    // Swash drawdown: the floor clamp pins the surface just ABOVE the seabed, which reads as a film of
    // water stuck at the old waterline while a wave recedes. Inside the swash shallows invert it to
    // just BELOW the seabed instead, so a receding surface sinks under the sand and the terrain
    // depth-cuts it — the waterline genuinely retreats. The release fades out over the swash reach;
    // open shallow water keeps the anti-hole clamp unchanged.
    // Burial depth is tweakable ("Swash drawdown (m)"): the receded surface meets the sand at a grazing
    // angle, and a deeper dive steepens that intersection so the retreat edge cuts clean instead of
    // sawtoothing along the clipmap triangles.
    const float swashReach = u_oceanParams7.w;
    // Drawdown 0 means NO drawdown — keep the anti-hole floor. The inversion has to be gated on the tweak
    // itself, not just clamped inside: max(x, eps) still buries 5 cm at x = 0, so the setting could not be
    // turned off from the UI, only turned down.
    if (u_oceanParams7.z > 0.0 && depth > 0.0 && u_oceanParams8.x > 0.0)
        floorY = mix(floorY, -depth - max(u_oceanParams8.x, eps), 1.0 - smoothstep(0.0, max(swashReach, 0.01), depth));
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
// Implicit-LOD sample for the surface-shading reads. The water fragment shader defines
// OCEAN_SURFACE_SAMPLE_BIAS ("Ocean/Shading/Glint sharpness", negative = finer mips): the shading
// normal then resolves wave detail the plain trilinear footprint would have filtered away, so the sun
// glint breaks into crisp glitter instead of following mip-softened blobs — and the LEAN variance
// below shrinks to match, since it measures exactly what the filtering removed. The bias overload of
// texture() is fragment-stage-only, so other stages (the foam compute) compile the plain call.
#ifdef OCEAN_SURFACE_SAMPLE_BIAS
#define OCEAN_SURFACE_TEX(uvw) texture(u_oceanMaps, uvw, OCEAN_SURFACE_SAMPLE_BIAS)
#else
#define OCEAN_SURFACE_TEX(uvw) texture(u_oceanMaps, uvw)
#endif

void oceanSampleSurface(vec2 worldXZ, out vec2 slope, out float jacobian, out vec2 slopeVar, out float accel)
{
    const float chop = u_oceanParams0.w;
    const vec2 shoreHW = oceanSampleShoreData(worldXZ); // (terrain height, water level)
    const float depth = shoreHW.y - shoreHW.x;
    // Buried under land: all shoal fades are zero — the loop would sum nothing (flat calm surface).
    // With swash on, the run-up band on land still shades: its geometry carries the raw wave residual.
    if (depth <= (u_oceanParams7.z > 0.0 ? -u_oceanParams7.w : 0.0))
    {
        slope = vec2(0.0);
        jacobian = 1.0;
        slopeVar = vec2(0.0);
        accel = 0.0;
        return;
    }
    const vec2 fr = oceanFlowRotation(worldXZ);           // same rotation as the displacement passes
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
        const vec4 g = OCEAN_SURFACE_TEX(vec3(uv, float(OCEAN_CASCADES + c)));
        const vec4 d = OCEAN_SURFACE_TEX(vec3(uv, float(c)));
        const vec4 m = OCEAN_SURFACE_TEX(vec3(uv, float(2 * OCEAN_CASCADES + c)));
        // Shoaling: fade this cascade's contribution with the SAME factor the displacement uses, so the
        // shading (slopes/folds/variance) tracks the flattened shallow-water geometry (variance ~ fade^2).
        const float fade = oceanShoalFade(depth, u_oceanParams2[c]);
        slopeSum += g.xy * fade;
        varSum += max(m.xy - g.xy * g.xy, vec2(0.0)) * (fade * fade);
        sxx += g.z * fade; szz += g.w * fade; sxz += d.w * fade;
        accel += m.z * fade;
        rawSlope += g.xy;
        rawVar += max(m.xy - g.xy * g.xy, vec2(0.0));
        rawAccel += m.z;
    }
    // Swash run-up: the displacement rides the RAW wave height through the waterline (scaled by
    // amplitude, land-height fade and the inverse shoal fade — oceanSampleDisplacement), so the shading
    // slopes/variance/accel must carry the same residual or the tongue shades as a flat mirror sheet.
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
    // Chop-corrected slope (Tessendorf), with a GENTLE floor and a rational soft limit on the magnitude:
    // near folds the (1 + lambda dD) terms approach zero and the raw division explodes the slope, which
    // tips the normal past grazing and shades as harsh dark creases along every crest.
    slope = slopeSum / max(vec2(jxx, jzz), vec2(0.6));
    slope /= 1.0 + 0.2 * length(slope);
    slope = oceanFlowToWorld(slope, fr); // back into world space (jacobian/variance are near rotation-invariant)
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
    if (depth <= 0.0)
        return vec3(0.0, 1.0, 0.0); // buried under land: all shoal fades are zero — flat
    const vec2 fr = oceanFlowRotation(worldXZ);           // same rotation as the displacement passes
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
