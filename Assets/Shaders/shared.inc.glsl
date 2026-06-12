#ifndef SHARED_CONSTANTS_INC_GLSL
#define SHARED_CONSTANTS_INC_GLSL

// ALPHA_MODE_* (RendererVKLayout::EAlphaMode) and MATERIAL_FLAG_* (RendererVKLayout::MATERIAL_FLAG_*)
// are injected by the engine from Layout.ixx.
// MATERIAL_FLAG_NO_RAYTRACING: debug/gizmo geometry that never blocks light — excluded from the TLAS
// (mask 0) and from the sun cascade caster cull.
// MATERIAL_FLAG_SKY: sky sphere, skipped in the G-buffer prepass so its depth stays at the far plane
// (parallax-free TAA).

const uint EMPTY_ENTRY        = 0xFFFFFFFFu;
const uint INITIALIZING_ENTRY = 0xEFFFFFFFu;

const float PI = 3.14159265359;

#include "ubo.inc.glsl"

vec3 randomColor(uint seed) 
{
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    vec3 bits = vec3(float(seed & 255u), float((seed >> 8) & 255u), float((seed >> 16) & 255u));
    return bits / 255.0;
}
vec3 randomColor(ivec3 seed) 
{
    uvec3 u = uvec3(seed);
    uint hash = u.x * 1597334673u + u.y * 3812015801u + u.z * 2798796415u;
	return randomColor(hash);
}
vec3 cascadeDebugColor(int cascade)
{
	if (cascade == 0) return vec3(1.0, 0.0, 0.0);
	if (cascade == 1) return vec3(0.0, 1.0, 0.0);
	if (cascade == 2) return vec3(0.0, 0.0, 1.0);
    if (cascade == 3) return vec3(1.0, 0.0, 1.0);
    if (cascade == 4) return vec3(0.0, 1.0, 1.0);
	return vec3(1.0, 1.0, 0.0);
}

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

vec3 skyGradient(float up)
{
    // Below the horizon: smoothstep instead of the clamped linear ramp, so the ground fade eases out of the
    // horizon color (no slope kink at up = 0) and eases into the ground color (no clamp kink at up = -0.5).
    return (up >= 0.0) ? mix(u_skyHorizon, u_skyZenith, smoothstep(0.0, 0.5, up)) : mix(u_skyHorizon, u_skyGround, smoothstep(0.0, 0.05, -up));
    //return mix(mix(u_skyHorizon, u_skyZenith, smoothstep(0.0, 0.5, up)), mix(u_skyHorizon, u_skyGround, smoothstep(0.0, 0.5, -up)), up + 1.0);
     //return (up >= 0.0) ? mix(u_skyHorizon, u_skyZenith, sqrt(up)) : mix(u_skyHorizon, u_skyGround, smoothstep(0.0, 0.5, -up));
}

// Cheap analytic approximation of the raymarched sky (sky.fs.glsl) for GI/AO miss rays: the configured
// gradient, dimmed toward night by sun elevation, with a warm halo toward the sun standing in for the
// atmosphere's forward scattering. Intentionally approximate (a few ALU per miss ray).
vec3 skyColor(vec3 dir)
{
    vec3 up = normalize(u_skyUp);
    vec3 sunDir = normalize(u_sunDirection.xyz);
    float t = dot(dir, up);
    vec3 sky = skyGradient(t);
    float dayLight = clamp(dot(sunDir, up) * 3.0 + 0.05, 0.0, 1.0);
    float halo = pow(clamp(dot(dir, sunDir), 0.0, 1.0), 6.0);
    return sky * dayLight * (1.0 + halo);
}

// move somewhere cleaner
vec3 skyRadiance(vec3 dir)
{
    vec3 sky = skyColor(dir);
    return (sky * u_skyIntensity) / PI;
}

#endif
