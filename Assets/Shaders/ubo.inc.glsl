#ifndef UBO_INC_GLSL
#define UBO_INC_GLSL

#ifndef UBO_BINDING
#define UBO_BINDING 0
#endif

#define NUM_SHADOW_CASCADES 6

layout (binding = UBO_BINDING, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
    float u_giIntensity;   // multiplier on global illumination

    vec3 u_sunDirection;   // xyz = normalized direction towards the sun, w unused
    float u_sunAngularCos; // cos of the sun disc radius (1 = point, smaller = bigger disc)
    vec3 u_sunColor;       // rgb = color * intensity
    float u_sunGlow;       // glow falloff exponent (0 = no glow); larger = tighter

    vec3  u_skyZenith;     // color along +skyUp
    float u_skyIntensity;
    vec3  u_skyHorizon;    // horizon color (perpendicular to skyUp)
    float u_ambientIntensity; // multiplier on the ambient term (horizon + zenith, without GI)
    vec3  u_skyGround;     // color along -skyUp
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
    vec4 u_cloudParams1;    // x = edge softness, y = sun shading strength, z = silver lining, w = ambient amount
    vec4 u_cloudParams2;    // x = density (extinction), y = sharpness, z = base/top height variation, w = moon brightness
    vec4 u_skySunParams;    // x = atmosphere scatter boost, y = Mie anisotropy g, z = sun disc feather, w = star density

    mat4 u_invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv
    mat4 u_prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen
    mat4 u_prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos

    vec4 u_screenSize;   // xy = full render-target resolution (px); zw = 1/xy
    vec4 u_viewportRect; // xy = viewport min, zw = viewport size, normalized to [0,1] of the full render target
    vec4 u_taaJitter;    // xy = TAA sub-pixel jitter in NDC, applied in clip space by raster vertex shaders only

    // Volumetric fog (packing documented in vol_fog.inc.glsl)
    vec4 u_fogParams0;   // x = global density (1/m), y = height base, z = height falloff (1/m), w = range (m)
    vec4 u_fogParams1;   // rgb = fog albedo, w = phase anisotropy g
    vec4 u_fogParams2;   // x = noise scale (1/m), y = noise strength, z = wind speed (m/s), w = temporal blend
    vec4 u_fogParams3;   // x = sun boost, y = ambient boost, z = enabled, w = light shadow rays
    vec4 u_fogParams4;   // x = sun shadow rays, y = spatial filter, z = GI ambient, w = sun shadow softness (rad)

    vec4 u_moonParams;   // xyz = normalized direction towards the moon, w = cos of the moon disc radius

    vec4 u_starParams;   // x = star size, y = size variation, z = brightness, w = color variation
    vec4 u_nebulaParams; // x = intensity, y = noise scale, z = band width, w = dust lane strength
    vec4 u_nebulaAxis;   // xyz = normalized milky-way band pole (band lies on its great circle), w unused
};

#endif