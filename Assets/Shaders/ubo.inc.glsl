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
    float _padShadow0;
    float _padShadow1;
    float _padShadow2;

    mat4 u_invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv
    mat4 u_prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen
    mat4 u_prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos

    vec4 u_screenSize;   // xy = full render-target resolution (px); zw = 1/xy
    vec4 u_viewportRect; // xy = viewport min, zw = viewport size, normalized to [0,1] of the full render target
    vec4 u_taaJitter;    // xy = TAA sub-pixel jitter in NDC, applied in clip space by raster vertex shaders only
};

#endif