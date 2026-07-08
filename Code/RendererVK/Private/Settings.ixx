export module RendererVK:Settings;

import Core;
import Core.glm;

// Renderer configuration parameter blocks. Each exposes itself to the TweakPanel via registerTweaks();
// the Renderer owns one instance of each and feeds them into the per-frame UBO / push constants.

// Everything in the TweakPanel's "Sky" categories (Sky / Sky/Sun / Sky/Atmosphere / Sky/Clouds /
// Sky/Stars / Sky/Nebula / Sky/Moon).
export struct SkyParams
{
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f); // sky "up" axis; also the sky radiance light direction

    // Sun
    glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.4f, 0.91f, 0.09f));
    glm::vec3 sunColor = glm::vec3(0.9568f, 1.0f, 0.9214f);
    float sunIntensity = 3.0f;
    float sunAngularCos = 0.99998f;     // cos of the disc radius (1 = disc off)
    float sunGlow = 1.0f;               // sun halo strength (0 = none, ~0.5 subtle, 2 = heavy); HG forward lobe in sky.fs
    float sunRolloff = 1.25f;           // sky highlight roll-off: soft-clips the overexposed sun region so its gradient survives (0 = raw hard clip)
    float sunRolloffKnee = 0.75f;       // luminance where compression starts at full roll-off (lower = more range compressed)
    float sunRolloffHeadroom = 6.0f;    // brightness range the shoulder absorbs (higher = brighter values stay distinguishable)
    float shadowDepthBias = 0.0002f;    // sun cascade depth bias
    float shadowNormalBias = 0.0f;      // sun cascade normal bias (texels)

    // Ambient + sky radiance (the non-sun lighting inputs)
    glm::vec3 ambientColor = glm::vec3(1.0f);   // flat non-physical minimum ambient
    float ambientIntensity = 0.003f;
    glm::vec3 skyRadianceColor = glm::vec3(0.55f, 0.65f, 1.0f); // directional sky radiance (moonlight/space light), along up
    float skyRadianceIntensity = 0.05f;

    // Ground plane of the sky sphere (below the horizon): albedo lit by sun + sky. Also tints the
    // ground-bounce fallback in skyRadiance() for downward GI/fog rays.
    glm::vec3 groundColor = glm::vec3(0.0f, 0.17f, 1.0f);
    float groundIntensity = 0.0f;

    // Atmosphere scattering: multipliers on the Earth sea-level Rayleigh/Mie coefficients. These drive
    // both the visible sky and (through skyRadiance) the majority of the indirect sky lighting.
    float rayleighScatter = 1.0f;
    float mieScatter = 1.0f;
    float scatterBoost = 3.0f;          // in-scatter multiplier: how much sunlight the atmosphere scatters (more = more indirect light)
    float mieG = 0.76f;                 // Mie anisotropy (forward-scatter lobe)
    float rayleighHeight = 8500.0f;     // Rayleigh scale height (m): how fast air density falls off
    float mieHeight = 1200.0f;          // Mie scale height (m): how high the haze layer reaches
    float mieExtinction = 1.11f;        // Mie extinction/scattering ratio (> 1 = absorbing haze)
    float ozone = 1.0f;                 // ozone absorption strength (1 = Earth-like); absorbs green/yellow,
                                        // suppresses the green horizon band single scattering produces

    // Clouds
    float cloudCoverage = 0.35f;
    float cloudHeight = 6300.0f;        // meters above the viewer (slab base)
    float cloudThickness = 86.0f;    // slab thickness sqrt(m)
    float cloudScale = 0.7f;            // multiplier on the base noise frequency
    float cloudWindSpeed = 3.00f;       // multiplier on the base wind drift
    float cloudWindAngle = 0.0f;       // radians
    float cloudSoftness = 0.26f;        // density smoothstep width (small = crisp edges)
    float cloudDensity = 1.58f;          // extinction strength (high = opaque cores)
    float cloudSharpness = 1.0f;       // density remap contrast (high = hard-edged shapes)
    float cloudBaseVar = 0.78f;          // per-column base/top height variation (0 = flat slab)
    float cloudShading = 2.00f;          // directional sun-shading strength

    // Stars
    float starDensity = 0.63f;
    float starSize = 1.3f;              // base star core size multiplier
    float starSizeVar = 0.62f;          // 0 = uniform size, 1 = full per-star variation (skewed small)
    float starBrightness = 1.23f;
    float starColorVar = 0.85f;         // 0 = white, 1 = full cool/warm per-star tint

    // Nebula (milky-way band)
    float nebulaIntensity = 0.2f;       // band glow strength (0 = off)
    float nebulaScale = 5.7f;           // noise frequency
    float nebulaBandWidth = 0.1f;       // gaussian width of the band around its great circle
    float nebulaDust = 1.0f;            // dark dust lane strength inside the band
    glm::vec3 nebulaAxis = glm::normalize(glm::vec3(0.706f, -0.418f, 0.572f)); // band pole

    // Moon
    glm::vec3 moonDirection = glm::normalize(glm::vec3(0.728f, 0.659f, -0.190f)); // independent of the sun
    float moonSizeDeg = 6.0f;           // disc radius (degrees); real moon is ~0.26
    float moonBrightness = 0.3f;

    void registerTweaks();
};

// Volumetric fog (froxel grid; see VolumetricFogPipeline) — the TweakPanel's "Fog" categories.
// All UBO-driven, so changes apply live.
export struct FogParams
{
    bool  enabled = true;
    float density = 0.020f;        // global extinction at the height base (1/m)
    float heightBase = 0.0f;       // world height where the global fog is densest
    float heightFalloff = 0.40f;   // exponential density falloff above the base (1/m)
    glm::vec3 albedo = glm::vec3(1.0f, 1.0f, 1.0f);
    float albedoIntensity = 1.0f;  // > 1 is a non-physical gain (emissive-ish fog)
    float anisotropy = 0.15f;      // HG phase g (0 = isotropic, ->1 = forward scattering)
    float range = 1024.0f;         // froxel grid far distance (m)
    float noiseScale = 0.08f;      // density noise frequency (1/m)
    float noiseStrength = 0.5f;    // 0 = uniform fog, 1 = fully modulated (dusty wisps)
    float windSpeed = 1.5f;        // noise drift (m/s)
    float temporalBlend = 0.9f;    // history blend weight (jittered Z integration)
    bool  lightShadows = true;     // shadow ray per froxel per grid light (expensive)
    int   sunRays = 1;             // sun shadow rays per froxel (RT sun mode); main perf knob
    float sunSoftness = 0.02f;     // shadow ray cone half-angle (rad); softens + decorrelates the rays
    bool  spatialFilter = true;    // 3x3 tent on the scatter grid in the integrate pass
    bool  giAmbient = true;        // GI probe ambient (off = analytic sky only, cheaper)

    void registerTweaks();
};

// Exposure + tonemapping, applied in the composite pass (the HDR -> display mapping) — the
// TweakPanel's "Post" category. Baked into the composite push constants, so changes re-record.
export struct PostParams
{
    float exposureEV = 0.0f; // exposure in stops; manual exposure, or exposure compensation in auto mode
    int   tonemapper = 2;    // 0 = off (legacy raw clip), 1 = Reinhard, 2 = ACES, 3 = AgX
    bool  autoExposure = true; // eye adaptation: drive exposure from scene luminance
    float adaptTau = 3.0f;   // adaptation time constant (s); larger = slower eye
    float adaptKey = 0.18f;  // target middle-grey luminance
    float adaptMinLogLum = -3.14f; // histogram log2-luminance range
    float adaptMaxLogLum = 4.0f;
    float adaptMinEV = -6.0f; // auto-exposure clamp (stops)
    float adaptMaxEV = 6.0f;

    // onReRecord is invoked when a tweak that's baked into the composite push constants changes, so the
    // command buffers can be re-recorded.
    void registerTweaks(const std::function<void()>& onReRecord);
};

export struct RTParams
{
    bool rtSunShadow = false;   // sun shadows from TLAS ray queries instead of PCSS cascades (A/B tweak)
    int  sunShadowRays = 5;     // RT sun shadow rays per pixel
    bool rtLightShadows = true; // ray-traced shadows for punctual/area/tube lights
    bool rtSkyRadiance = true;  // ray-traced sky visibility for the sky radiance light (GI probe trace)
    int  blasLodLevel = 1;      // LOD level whose geometry backs a chain's single shared BLAS (clamped per
                                // chain; rays don't need per-level fidelity). Applied when containers load.
    bool blasCompaction = true; // copy-compact static BLASes after build (~30-50% of their memory back);
                                // applies to BLASes built after a change

    void registerTweaks();
};

export struct RTAOParams
{
    bool  enabled = true;
    int   rays = 6; //1;
    float radius = 1.0f;
    float power = 1.5f;
    float intensity = 1.0f;
    float fadeStart = 30.0f;   // distance from camera (world units) where AO begins to fade out
    float maxDistance = 60.0f; // distance at which AO is fully gone; 0 disables the falloff
    float maxHistory = 0.77; //0.99f;
    int   blurRadius = 2; //0;
    bool  alphaTest = false; // ray-test alpha-masked geometry (vegetation) instead of treating it as solid

    void registerTweaks(const std::function<void()>& onReRecord, const std::function<void()>& onReloadShaders);
};

export struct TAAParams
{
    bool  taaEnabled = true;
    float taaFeedback = 0.9f;

    void registerTweaks(const std::function<void()>& onReRecord);
};

// Mesh LOD chains (authored "LodN_*" meshes and/or meshopt-generated) — the TweakPanel's "LOD" category.
// Selection runs per instance at renderNode time; generate/generateLevels/minIndices are read at
// ObjectContainer load, so they only affect containers loaded after a change.
export struct MeshLodParams
{
    bool  enabled = true;         // per-instance LOD selection (off = everything renders LOD0)
    float maxErrorPixels = 0.33f;  // screen-space error budget: coarsest level whose geometric deviation projects below this is used (generated chains carry per-level meshopt errors)
    float fullResPixels = 256.0f; // FALLBACK metric for chains without error data (authored LodN_): projected diameter (px) above which LOD0 is used; each halving drops one level
    int   bias = 0;               // coarseness bias: doubles the error budget per step (fallback: levels added)
    float hysteresis = 0.25f;     // switch dead-band: fraction of the error budget (fallback: fraction of a level) a change must overshoot before switching
    int   forceLod = -1;          // >= 0: clamp every LOD instance to this level (debug)
    bool  generate = true;        // meshopt-generate chains for static meshes without authored LODs
    int   generateLevels = 4;     // max generated levels beyond LOD0
    float generateReduction = 0.5f; // index-count factor per generated level (0.25 = quarter the triangles)
    int   minIndices = 32;        // don't generate for meshes below this index count

    void registerTweaks();
};

// FFT/Tessendorf ocean: spectrum inputs for the GPU simulation (OceanSimulationPipeline) + water shading.
// The Renderer feeds these into the per-frame UBO (ocean* fields), which drives BOTH the compute simulation
// (the TMA spectrum is re-evaluated every frame, so all of it is live) and the surface shading. The grid
// geometry + Tweaks live in Procedural::OceanRenderer, which builds one of these each frame and hands it to
// Renderer::setOceanParams.
export struct OceanParams
{
    bool enabled = false;       // gates the per-frame FFT simulation + ocean draw

    // Spectrum (TMA = JONSWAP x Kitaigorodskii finite-depth attenuation, Hasselmann directional spreading;
    // Horvath 2015). Dispersion is finite-depth: w^2 = g k tanh(k D).
    glm::vec2 windDirection = glm::normalize(glm::vec2(0.8f, 0.35f)); // dominant wave travel direction (XZ)
    float windSpeed   = 10.5f;  // U10 wind speed (m/s): the main sea-state knob
    float fetchKm     = 300.0f; // fetch (km): distance the wind has blown over; longer = bigger swell
    float depth       = 100.0f; // ocean depth D (m): finite-depth dispersion + TMA shallow-water attenuation
                                // (shallow values like 35 visibly mute the long swell — by design)
    float amplitude   = 1.0f;   // artistic scale on the spectrum amplitude (1 = physical)
    float choppiness  = 1.1f;   // horizontal displacement lambda (0 = heightfield only, higher = sharper crests)
    float normalStrength = 1.0f; // artistic scale on the shading slopes
    glm::vec3 cascadeSizes = glm::vec3(384.0f, 47.0f, 6.3f); // FFT patch sizes (m); non-rational ratios hide tiling
    float seaLevel    = 0.0f;   // world Y of the calm water plane

    // Optics: extinction drives Beer-Lambert absorption of the ray-traced refraction (1/m, Jerlov-ish
    // coastal water); scatter is the in-scattered radiance albedo (the water's "color" in deep water).
    glm::vec3 absorption   = glm::vec3(0.42f, 0.085f, 0.04f);
    glm::vec3 scatterColor = glm::vec3(0.012f, 0.08f, 0.085f);
    float scatterStrength  = 1.0f;
    float roughness        = 0.07f; // perceptual micro-roughness (widens the sun glint)
    // Foam & turbulence. ONE instant-foam response (oceanInstantFoam) both draws the per-pixel crest
    // foam and injects the accumulated TURBULENCE field (the churn energy breaking leaves behind).
    // Turbulence then drives the wake look: it relaxes the fold threshold (aged foam paints itself along
    // the LIVE geometry's convergence lines) and makes the water milky + rough (entrained bubbles).
    glm::vec3 foamColor    = glm::vec3(0.88f, 0.92f, 0.94f);
    float foamBias         = 0.6f;  // fold threshold: Jacobian below this is folding (foaming)
    float foamBreakAccel   = 0.25f; // breaking threshold (Longuet-Higgins): downward crest acceleration
                                    // above this fraction of g is breaking — what makes LARGE waves foam
    float foamSoftness     = 0.5f;  // edge width of both thresholds (small = crisp crest lines)
    float foamDecay        = 0.985f; // turbulence retention per frame (wake persistence)
    float foamSpread       = 1.2f;  // turbulence diffusion per frame: the wake spreads as it lives (also
                                    // keeps the stored field free of texel structure)
    float foamBoost        = 0.6f;  // how much turbulence relaxes the fold threshold: the aged-foam
                                    // amount in the wake (0 = only actively breaking crests foam)
    float turbidity        = 0.4f;  // entrained-bubble strength: milky brightening + extra roughness of
                                    // turbulent water (the wake stays visible after the foam thins)

    // Grid cell size as a linear function of view distance (cell = a*dist + b), computed by
    // Procedural::OceanRenderer from its graded grid so the vertex shaders can pick the Nyquist-safe
    // displacement mip for the local vertex density (undersampling the maps makes the surface alias,
    // which reads as waves snapping around as the grid follows the camera).
    float gridCellA = 0.04f;
    float gridCellB = 1.2f;
};

export struct Stats
{
    uint32 numLights;
    uint32 maxLights;

    uint32 numMeshInstances;
    uint32 maxMeshInstances;

    uint32 numInstanceOffsets;
    uint32 maxInstanceOffsets;

    uint32 numMeshTypes;
    uint32 maxMeshTypes;

    uint32 numMaterials;
    uint32 maxMaterials;

    uint32 numRenderNodes;
    uint32 maxRenderNodes;

    uint32 numTextures;
    uint32 maxTextures;

    uint64 vertexDataUsedBytes;
    uint64 maxVertexDataBytes;

    uint64 indexDataUsedBytes;
    uint64 maxIndexDataBytes;

    uint32 numObjectContainers;

    uint32 numLightGrids;
    uint32 maxLightGrids;

    uint64 lightGridMemUsageBytes;
    uint64 maxLightGridMemUsageBytes;

    // Total GPU memory tracked by VMA across all heaps.
    uint64 gpuMemoryUsedBytes;     // bytes our live allocations occupy
    uint64 gpuMemoryReservedBytes; // bytes VMA has reserved in blocks (>= used)
    uint64 gpuMemoryBudgetBytes;   // device-local budget available to the process

    // Texture mip streaming (see TextureStreamer).
    uint64 textureBudgetBytes;
    uint64 textureResidentBytes;   // live allocations of streamable textures
    uint64 texturePinnedBytes;     // unstreamable textures, always fully resident
    uint64 textureDesiredBytes;    // what the priority pass wants resident
    uint64 textureTailBytes;       // always-resident mip tails (part of resident)
    uint32 numStreamableTextures;
    uint32 numStreamOpsInFlight;

    // Static BLAS memory (see AccelerationStructure; excludes the per-frame skinned BLASes).
    uint64 blasBytes;
    uint64 blasCompactionSavedBytes; // cumulative bytes reclaimed by copy-compaction

    // Mesh data streaming (see MeshStreamer).
    uint64 meshBudgetBytes;
    uint64 meshStreamableBytes; // registered mesh sets, resident or not
    uint64 meshResidentBytes;
    uint64 meshColdBytes;       // resident but unseen long enough to be eviction candidates
    uint32 numMeshSets;
    uint32 numEvictedMeshSets;

    // Mesh LOD (see MeshLodParams / Renderer::selectMeshLods).
    uint32 numMeshLodGroups;
    uint32 lodInstanceCounts[5];   // LOD instances rendered per selected level this frame (MAX_MESH_LODS)
};