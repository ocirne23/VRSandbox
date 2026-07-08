#version 460

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Ocean water shading (EPipelineIndex::Ocean). The vertex shader (instanced_indirect.vs.glsl with OCEAN)
// Gerstner-displaced the grid and passed the *undisplaced* world XZ through in_uv; here the wave field is
// re-evaluated at higher detail for a per-pixel analytic normal + fold factor, then the surface is shaded
// with a small stack of published water/PBR techniques:
//   - Cook-Torrance microfacet sun glint: GGX/Trowbridge-Reitz NDF (Walter et al. 2007; Karis UE4 2013),
//     height-correlated Smith masking-shadowing visibility (Heitz 2014), Schlick Fresnel (Schlick 1994)
//     with the F0 = 0.02 of water.
//   - Geometric specular anti-aliasing (Kaplanyan et al. 2016; Filament): roughen by the sub-pixel normal
//     variance from screen-space derivatives, so the glint stops sparkling on the fine ripples at distance.
//   - Fresnel blend to a roughness-blurred sky reflection (a cheap prefiltered-environment stand-in).
//   - Subsurface translucency (Barré-Brisebois & Bouchard, GDC 2011): the teal glow of the sun coming
//     through thin, backlit wave crests, plus a diffuse upwelling term.
//   - Jacobian-fold whitecap foam.
// Reflection/ambient reuse the engine's atmosphere model (skyRadiance, atmosphere.inc.glsl via
// shared.inc.glsl), so the water tracks the live sky/sun tweaks. Self-contained: only the UBO is read.

#include "shared.inc.glsl"
#include "ocean_wave.inc.glsl"

layout (location = 0) in vec3 in_pos;                       // displaced world position
layout (location = 1) in mat3 in_tbn;                       // coarse wave TBN (unused: normal recomputed here)
layout (location = 4) in vec2 in_uv;                        // undisplaced world XZ
layout (location = 5) in flat uint in_meshIdxMaterialIdx;
#ifdef STEREO
layout (push_constant) uniform ViewPC { uint u_viewIndex; }; // selects the per-eye view (1=left, 2=right) in VR
#endif

layout (location = 0) out vec4 out_color;

// GGX / Trowbridge-Reitz NDF (Walter et al. 2007), Karis' optimised form. a = alpha = roughness^2.
float D_GGX(float NoH, float a)
{
    float a2 = a * a;
    float d  = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}
// Height-correlated Smith masking-shadowing visibility (Heitz 2014); already includes the 1/(4 NoV NoL).
float V_SmithGGX(float NoV, float NoL, float a)
{
    float a2 = a * a;
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}
vec3 F_Schlick(float u, vec3 F0)
{
    float x = clamp(1.0 - u, 0.0, 1.0);
    float x2 = x * x;
    return F0 + (1.0 - F0) * (x2 * x2 * x);
}
// Geometric specular AA (Kaplanyan et al. 2016 / Filament): widen the microfacet roughness by the
// screen-space normal variance so the specular lobe covers the sub-pixel normal spread. Input/return alpha.
float specularAA(vec3 N, float alpha)
{
    vec3 dNdx = dFdx(N);
    vec3 dNdy = dFdy(N);
    float variance = 0.25 * (dot(dNdx, dNdx) + dot(dNdy, dNdy)); // SIGMA^2
    float kernel   = min(2.0 * variance, 0.18);                  // clamp the added roughness (KAPPA)
    return sqrt(clamp(alpha * alpha + kernel, 0.0, 1.0));
}

void main()
{
#ifdef STEREO
    g_viewIndex = int(u_viewIndex);
#endif
    const vec3 up = normalize(u_skyUp);
    const vec3 L  = normalize(u_sunDirection.xyz);
    const vec3 V  = normalize(u_viewPos - in_pos);

    // Per-pixel wave normal + fold factor from the undisplaced world XZ (finer than the vertex normal).
    vec3 disp, N;
    float jacobian;
    oceanEval(in_uv, OCEAN_FRAG_WAVES, disp, N, jacobian);
    // Tilt the large-scale wave normal by the domain-warped noise-FBM slope: random high-frequency ripple
    // that breaks up the smooth Gerstner blobs (u_oceanParams1.w = strength). Fades with distance so the
    // far sea doesn't shimmer/alias (the detail is sub-texel out there anyway).
    const float viewDist = length(u_viewPos - in_pos);
    const float detailFade = clamp(1.0 - viewDist / 600.0, 0.0, 1.0);
    const vec2 detailSlope = oceanDetailSlope(in_uv) * (u_oceanParams1.w * 0.6 * detailFade);
    N = normalize(N + vec3(-detailSlope.x, 0.0, -detailSlope.y));
    if (dot(N, V) < 0.0) // double-sided (camera skimming / below the surface)
        N = -N;

    const float NoV = clamp(dot(N, V), 1e-3, 1.0);
    const float NoL = max(dot(N, L), 0.0);
    const vec3  H   = normalize(L + V);
    const float NoH = max(dot(N, H), 0.0);
    const float LoH = max(dot(L, H), 0.0);

    // Roughness -> alpha, filtered by the geometric specular-AA term (kills distant glint sparkle).
    const float perceptualRough = clamp(u_oceanDeepColor.w, 0.02, 1.0);
    const float alpha  = perceptualRough * perceptualRough;
    const float alphaF = specularAA(N, alpha);

    // Direct sun radiance, atmospherically attenuated + eclipse-scaled like the main surface lighting.
    const vec3 sunTint    = u_sunColor.rgb * atmosTransmittanceToLight(0.0, L, up) * u_eclipseParams.x;
    const vec3 ambientSky = skyRadiance(up); // overhead sky as the hemispherical ambient onto the water
    const vec3 F0 = vec3(0.02);              // water at normal incidence

    // --- Refracted body: deep-water absorption + subsurface translucency (Barré-Brisebois 2011) ----------
    const float amp   = max(u_oceanParams0.z, 0.01);
    const float crest = clamp(disp.y / amp * 0.5 + 0.5, 0.0, 1.0);   // 0 = trough, 1 = crest ("thin" part)
    // Translucency: distort the light dir by the surface normal, then a back-facing view lobe; scaled by
    // the crest "thinness" so the glow reads through backlit wave peaks.
    const vec3  sssDir = normalize(L + N * 0.35);
    const float sss    = pow(clamp(dot(V, -sssDir), 0.0, 1.0), 4.0) * crest;
    const vec3  translucency = u_oceanScatterColor.rgb * u_oceanScatterColor.w * sss * (sunTint + 0.4 * ambientSky);
    // Diffuse upwelling of in-scattered skylight, brighter on crests.
    const vec3  upwelling = u_oceanScatterColor.rgb * u_oceanScatterColor.w * (0.15 + 0.35 * crest) * ambientSky * 0.5;
    vec3 body = u_oceanDeepColor.rgb * (ambientSky + sunTint * (0.35 * NoL) + u_ambientColor) + upwelling + translucency;

    // --- Reflection: mirror sky, blurred toward the average sky by the (AA-filtered) roughness -----------
    vec3 R = reflect(-V, N);
    R.y = max(R.y, 0.02); // keep grazing reflections just above the horizon (never through the ground)
    const vec3  skyRefl = skyRadiance(normalize(R));
    const float reflBlur = clamp(sqrt(alphaF) * 2.0 - 0.05, 0.0, 0.6);
    const vec3  reflection = mix(skyRefl, ambientSky, reflBlur);

    // Fresnel: ~2% reflective looking straight down (you see the body), ->100% at grazing (mirror sky).
    const float fresnel = F_Schlick(NoV, F0).x;
    vec3 color = mix(body, reflection, fresnel);

    // --- Sun glint: full Cook-Torrance specular (GGX D, height-correlated Smith V, Schlick F) ------------
    const float D  = D_GGX(NoH, alphaF);
    const float Vv = V_SmithGGX(NoV, NoL, alphaF);
    const vec3  Fs = F_Schlick(LoH, F0);
    color += sunTint * (D * Vv * NoL) * Fs;

    // --- Whitecap foam where the trochoid folds (Jacobian < threshold) or on the steepest crests --------
    float foam = 1.0 - smoothstep(u_oceanFoamColor.w - 0.5, u_oceanFoamColor.w, jacobian);
    foam = max(foam, smoothstep(0.9, 1.0, crest) * 0.4);
    const vec3 foamLit = u_oceanFoamColor.rgb * (0.5 * ambientSky + sunTint * NoL + u_ambientColor);
    color = mix(color, foamLit, clamp(foam, 0.0, 1.0));

    out_color = vec4(color, 1.0);
}
