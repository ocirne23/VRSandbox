#ifndef UBO_INC_GLSL
#define UBO_INC_GLSL

#ifndef UBO_BINDING
#define UBO_BINDING 0
#endif

// NUM_SHADOW_CASCADES is injected by the engine from RendererVKLayout (Layout.ixx)

layout (binding = UBO_BINDING, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
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

    mat4 u_invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv
    mat4 u_prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen
    mat4 u_prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos

    vec4 u_screenSize;   // xy = full render-target resolution (px); zw = 1/xy
    vec4 u_viewportRect; // xy = viewport min, zw = viewport size, normalized to [0,1] of the full render target
    vec4 u_taaJitter;    // xy = TAA sub-pixel jitter in NDC, applied in clip space by raster vertex shaders only

    // Volumetric fog (packing documented in vol_fog.inc.glsl)
    vec4 u_fogParams0;   // x = global density (1/m), y = height base, z = height falloff (1/m), w = range (m)
    vec4 u_fogParams1;   // rgb = fog albedo * intensity (> 1 = non-physical gain), w = phase anisotropy g
    vec4 u_fogParams2;   // x = noise scale (1/m), y = noise strength, z = wind speed (m/s), w = temporal blend
    vec4 u_fogParams3;   // xy = unused, z = enabled, w = light shadow rays
    vec4 u_fogParams4;   // x = sun shadow rays, y = spatial filter, z = GI ambient, w = sun shadow softness (rad)

    vec4 u_moonParams;   // xyz = normalized direction towards the moon, w = cos of the moon disc radius

    vec4 u_starParams;   // x = star size, y = size variation, z = brightness, w = color variation
    vec4 u_nebulaParams; // x = intensity, y = noise scale, z = band width, w = dust lane strength
    vec4 u_nebulaAxis;   // xyz = normalized milky-way band pole (band lies on its great circle), w unused

    vec4 u_eclipseParams; // x = visible sun fraction (solar eclipse; u_sunColor is pre-multiplied by it,
                          // the sky divides it back out for the unoccluded disc/corona),
                          // y = sky highlight roll-off headroom (stops the shoulder absorbs), zw unused

    vec4 u_atmosParams;   // x = Rayleigh scale height (m), y = Mie scale height (m), z = Mie extinction ratio, w = ozone strength

    vec4 u_groundParams;  // rgb = ground albedo * intensity (sky-sphere ground plane + the skyRadiance
                          // ground-bounce tint for downward GI/fog rays), w unused

    // Per-eye matrices (VR stereo). [0] mirrors the mono u_mvp/u_invMvp/u_viewPos/u_prev* above, so a
    // per-eye pass that selects eye 0 reproduces the mono path exactly (desktop and the left eye).
    mat4 u_mvpStereo[2];
    mat4 u_invMvpStereo[2];
    vec4 u_viewPosStereo[2];
    mat4 u_prevMvpStereo[2];
    mat4 u_prevInvMvpStereo[2];
};

// Per-eye view index for the stereo screen-space passes. Set once at the top of main() from a push
// constant (VR) and left at 0 otherwise (desktop / mono); the helpers below index the u_*Stereo[] arrays
// with it. Eye 0's stereo matrices mirror the mono ones, so the default reproduces the mono behaviour.
int g_viewIndex = 0;

#endif