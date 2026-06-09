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

    mat4 u_invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv
    mat4 u_prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen
    mat4 u_prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos

    vec4 u_screenSize;   // xy = full render-target resolution (px); zw = 1/xy
    vec4 u_viewportRect; // xy = viewport min, zw = viewport size, normalized to [0,1] of the full render target
    vec4 u_taaJitter;    // xy = TAA sub-pixel jitter in NDC, applied in clip space by raster vertex shaders only
};

// Reconstruct world-space position from a hardware depth sample and a full-frame screen UV (origin top-left),
// given an inverse view-proj. The scene renders through u_viewportRect (a sub-rect of the render target, e.g.
// the editor viewport panel), so the full-frame UV is first remapped into viewport-local [0,1], then to NDC.
// The viewport is y-flipped (negative height), so viewport-local v=0 (top) maps to ndc.y=+1.
vec3 worldPosFromDepthMat(vec2 uv, float depth, mat4 invM)
{
    vec2 vpUv = (uv - u_viewportRect.xy) / u_viewportRect.zw;
    vec4 clip = vec4(vpUv.x * 2.0 - 1.0, 1.0 - vpUv.y * 2.0, depth, 1.0);
    vec4 world = invM * clip;
    return world.xyz / world.w;
}
vec3 worldPosFromDepth(vec2 uv, float depth) { return worldPosFromDepthMat(uv, depth, u_invMvp); }

// Project a world position to a previous-frame full-frame screen UV (inverse of the mapping above): NDC ->
// viewport-local UV -> full-frame UV through u_viewportRect.
vec2 prevScreenUV(vec3 worldPos, out float clipW)
{
    vec4 p = u_prevMvp * vec4(worldPos, 1.0);
    clipW = p.w;
    vec3 ndc = p.xyz / p.w;
    vec2 vpUv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return u_viewportRect.xy + vpUv * u_viewportRect.zw;
}

// move somewhere cleaner
vec3 skyRadiance(vec3 dir)
{
    // Configurable 3-stop gradient along the sky-up axis: ground (-up) -> horizon -> zenith (+up).
    float t = dot(dir, normalize(u_skyUp));
    vec3 sky = (t >= 0.0) ? mix(u_skyHorizon, u_skyZenith, sqrt(t))
                          : mix(u_skyHorizon, u_skyGround, min(-t * 2.0, 1.0));
    return sky * u_skyIntensity;
}



#endif