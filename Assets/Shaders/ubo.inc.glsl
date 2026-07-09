#ifndef UBO_INC_GLSL
#define UBO_INC_GLSL

#ifndef UBO_BINDING
#define UBO_BINDING 0
#endif

// NUM_SHADOW_CASCADES is injected by the engine from RendererVKLayout (Layout.ixx)

#define VIEW_CENTER 0 // shared passes + desktop; the eyes are 1 (left) and 2 (right) in VR

// One camera view's matrices grouped contiguously (AoS), matching RendererVKLayout::ViewData. u_views[0] =
// centre/combined view (the only view on desktop; in VR sized to the union of both eyes' FOV), [1] = left
// eye, [2] = right eye (VR only). Selected by g_viewIndex (see below + the u_mvp/... convenience macros).
struct ViewData
{
    mat4 mvp;
    mat4 invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv
    mat4 prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen
    mat4 prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos
    mat4 reprojClip; // prevMvp * inverse(mvp) fused in double precision on the CPU: current NDC + depth
                     // -> last frame's clip, precision independent of the camera's world position
    vec4 viewPos;    // xyz = world position
};

layout (binding = UBO_BINDING, std140) uniform UBO
{
    ViewData u_views[3];
    vec4 u_frustumPlanes[6];
    vec3 u_viewPad;        // padding (centre viewPos lives in u_views[]); keeps u_betaMie 16-byte aligned
    float u_betaMie;       // Mie scattering coefficient at sea level (1/m), drives sky + indirect sky light

    vec3 u_sunDirection;   // xyz = normalized direction towards the sun, w unused
    float u_sunAngularCos; // cos of the sun disc radius (1 = point, smaller = bigger disc)
    vec3 u_sunColor;       // rgb = color * intensity (sun irradiance; also sources atmosphere scattering)
    float u_sunGlow;       // glow falloff exponent (0 = no glow); larger = tighter

    vec3  u_betaRayleigh;  // Rayleigh scattering coefficients at sea level (1/m), drives sky + indirect
    float u_rolloffKnee;   // sky highlight roll-off: luminance where compression starts (at full roll-off)
    vec3  u_skyRadianceColor; // directional atmosphere light (moonlight/space light) radiance, along u_skyUp; GI-only
    float u_rtSkyRadiance;    // > 0.5: one sky-visibility ray per GI probe gates the sky radiance injection
    vec3  u_ambientColor;  // flat, non-physical minimum ambient radiance (applied once at final shading)
    uint  u_frameIndex;    // monotonic frame counter (RNG / temporal-rotation source)
    vec3  u_skyUp;         // sky "up" axis (normalized); need not be world +Y (e.g. planet surface normal)
    float u_rtSunShadow;   // > 0.5: ray-traced sun shadows instead of PCSS cascades

    mat4 u_cascadeViewProj[NUM_SHADOW_CASCADES];
    vec3 u_shadowParams; // x = depth bias, y = normal bias (texels), z = 1/resolution
    float u_sunShadowRays; // RT sun shadow rays per pixel (1 = single jittered ray)

    float u_rtLightShadows; // > 0.5: ray-traced shadows for punctual/area/tube lights
    float u_timeSeconds;    // elapsed app time (cloud wind / sky animation)
    float u_cloudCoverage;  // 0 = clear sky, 1 = overcast
    float u_cloudThickness; // cloud slab thickness (m)

    vec4 u_cloudParams0;    // x = layer height (m), y = noise scale, z = wind speed (noise units/s), w = wind angle (rad)
    vec4 u_cloudParams1;    // x = edge softness, y = sun shading strength, z = silver lining, w = unused
    vec4 u_cloudParams2;    // x = density (extinction), y = sharpness, z = base/top height variation, w = moon brightness
    vec4 u_skySunParams;    // x = atmosphere scatter boost (in-scatter only), y = Mie anisotropy g, z = sky highlight roll-off, w = star density

    vec4 u_screenSize;   // xy = full render-target resolution (px); zw = 1/xy
    vec4 u_viewportRect; // xy = viewport min, zw = viewport size, normalized to [0,1] of the full render target
    vec4 u_taaJitter;    // xy = TAA sub-pixel jitter in NDC, applied in clip space by raster vertex shaders only

    // Volumetric fog (packing documented in vol_fog.inc.glsl)
    vec4 u_fogParams0;   // x = global density (1/m), y = height base, z = height falloff (1/m), w = range (m)
    vec4 u_fogParams1;   // rgb = fog albedo * intensity (> 1 = non-physical gain), w = phase anisotropy g
    vec4 u_fogParams2;   // x = noise scale (1/m), y = noise strength, z = wind speed (m/s), w = temporal blend
    vec4 u_fogParams3;   // x = terrain follow fraction, y = 1 / near terrain map world size (0 = no map), z = enabled, w = light shadow rays
    vec4 u_fogParams4;   // x = sun shadow rays, y = spatial filter, z = GI ambient, w = sun shadow softness (rad)
    vec4 u_fogParams5;   // xy = terrain height map world center XZ (shared by both cascades), z = 1 / far cascade world size (0 = near only), w = baked terrain sea level
    vec4 u_fogParams6;   // x = slice power (Z distribution exponent), y = terrain shadow distance (m), zw = unused

    vec4 u_moonParams;   // xyz = normalized direction towards the moon, w = cos of the moon disc radius

    vec4 u_starParams;   // x = star size, y = size variation, z = brightness, w = color variation
    vec4 u_nebulaParams; // x = intensity, y = noise scale, z = band width, w = dust lane strength
    vec4 u_nebulaAxis;   // xyz = normalized milky-way band pole (band lies on its great circle), w unused

    vec4 u_eclipseParams; // x = visible sun fraction (solar eclipse; u_sunColor is pre-multiplied by it,
                          // the sky divides it back out for the unoccluded disc/corona),
                          // y = sky highlight roll-off headroom (stops the shoulder absorbs), zw unused

    vec4 u_atmosParams;   // x = Rayleigh scale height (m), y = Mie scale height (m), z = Mie extinction ratio, w = ozone strength

    vec4 u_groundParams;  // rgb = ground albedo * intensity (sky-sphere ground plane + the skyRadiance
                          // ground-bounce tint for downward GI/fog rays), w = horizon terrain fraction
                          // (skyGroundRadiance: fraction of every hemisphere treated as sunlit ground)
    vec4 u_aoParams;      // x = RTAO enabled (0/1), y = GI strength, zw unused
    vec4 u_giVisParams;   // x = Chebyshev variance floor (fraction of spacing), y = Chebyshev power, z = probe weight floor, w = mean scale (footprint widening)

    // Ocean (FFT/Tessendorf water; ocean_*.cs.glsl simulation + ocean.fs.glsl shading)
    vec4 u_oceanParams0;    // xy = wind direction (unit), z = spectrum amplitude scale, w = choppiness lambda
    vec4 u_oceanParams1;    // x = wind speed U10 (m/s), y = fetch (m), z = ocean depth D (m), w = normal strength
    vec4 u_oceanParams2;    // xyz = cascade FFT patch sizes L0/L1/L2 (m), w = sea level (world Y)
    vec4 u_oceanAbsorption; // rgb = water extinction sigma_t (1/m, Beer-Lambert), w = perceptual roughness
    vec4 u_oceanScatter;    // rgb = in-scatter albedo color, w = scatter intensity
    vec4 u_oceanFoam;       // rgb = foam albedo, w = Jacobian foam bias (higher = more whitecaps)
    vec4 u_oceanParams3;    // xy = shore depth map world center XZ, z = turbulence decay per frame,
                            // w = vertex displacement mip bias (Detail bias; ring cell size rides per vertex)
    vec4 u_oceanParams4;    // x = 1 / shore map world size (0 = no shore map),
                            // y = turbulence spread (diffusion/frame),
                            // z = shoal depth scale (waves fade below depth = scale * cascade patch size),
                            // w = instant-foam edge width (both thresholds' smoothstep)
    vec4 u_oceanParams5;    // x = foam boost (turbulence -> fold-threshold relaxation),
                            // y = turbidity (entrained-bubble milkiness + roughness),
                            // z = shore foam depth (m; surf band width at the waterline, 0 = off),
                            // w = breaking-crest foam threshold (downward crest accel in g units)
};

// View index selecting which u_views[] entry the convenience macros / reconstruction helpers read. Defaults
// to VIEW_CENTER (0): shared world-space passes and the whole desktop path leave it there. The per-eye
// screen-space passes set it once at the top of main() from a push constant (1 = left eye, 2 = right eye).
int g_viewIndex = 0;

// Convenience accessors so call sites read the current view's matrices without spelling out u_views[..].
// Shared passes get the centre view for free (g_viewIndex == VIEW_CENTER). A pass that needs a specific
// view regardless of g_viewIndex (e.g. fog apply sampling the centre-built froxel volume) must index
// u_views[] explicitly instead of using these.
#define u_mvp        u_views[g_viewIndex].mvp
#define u_invMvp     u_views[g_viewIndex].invMvp
#define u_prevMvp    u_views[g_viewIndex].prevMvp
#define u_prevInvMvp u_views[g_viewIndex].prevInvMvp
#define u_reprojClip u_views[g_viewIndex].reprojClip
#define u_viewPos    (u_views[g_viewIndex].viewPos.xyz)

#endif