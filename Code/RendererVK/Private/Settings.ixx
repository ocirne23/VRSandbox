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
    float shadowDepthBias = 0.0f;       // sun cascade depth bias
    float shadowNormalBias = 0.0f;      // sun cascade normal bias (texels)

    // Ambient + sky radiance (the non-sun lighting inputs)
    glm::vec3 ambientColor = glm::vec3(1.0f);   // flat non-physical minimum ambient
    float ambientIntensity = 0.005f;
    glm::vec3 skyRadianceColor = glm::vec3(0.55f, 0.65f, 1.0f); // directional sky radiance (moonlight/space light), along up
    float skyRadianceIntensity = 0.20f;

    // Ground plane of the sky sphere (below the horizon): albedo lit by sun + sky. Also tints the
    // ground-bounce fallback in skyRadiance() for downward GI/fog rays.
    glm::vec3 groundColor = glm::vec3(0.0f, 0.17f, 1.0f);
    float groundIntensity = 1.0f;

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
    float cloudCoverage = 0.45f;
    float cloudHeight = 6300.0f;        // meters above the viewer (slab base)
    float cloudThickness = 86.0f;    // slab thickness sqrt(m)
    float cloudScale = 0.7f;            // multiplier on the base noise frequency
    float cloudWindSpeed = 3.00f;       // multiplier on the base wind drift
    float cloudWindAngle = 0.0f;       // radians
    float cloudSoftness = 0.26f;        // density smoothstep width (small = crisp edges)
    float cloudDensity = 1.58f;          // extinction strength (high = opaque cores)
    float cloudSharpness = 1.0f;       // density remap contrast (high = hard-edged shapes)
    float cloudBaseVar = 0.78f;          // per-column base/top height variation (0 = flat slab)
    float cloudShading = 0.69f;          // directional sun-shading strength

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
    float albedoIntensity = 2.0f;  // > 1 is a non-physical gain (emissive-ish fog)
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
