#version 450

// Volumetric fog apply: fullscreen pass in the scene-color render pass (after the forward + sky draws,
// before TAA so the fog gets antialiased). Reconstructs each pixel's view depth from the G-buffer depth
// and samples the integrated fog volume.
// Blended with (srcColor = ONE, dstColor = SRC_ALPHA): out = inScatter + sceneColor * transmittance.
//
// Everything BEYOND the froxel volume's far plane is the analytic far field (see vol_fog.inc.glsl): a
// closed-form height-fog integral instead of more slices. Froxels are only worth their cost where the
// medium has structure — local lights, fog volumes, density noise — all of which have faded out by the
// far plane, leaving one exponential layer and one directional light, which is exactly the case that
// solves in closed form. So the volume's range is a QUALITY knob for the near field, not a view distance:
// keep it short (a km or so) and let this term carry the rest of the world, to the horizon and past it.

#include "shared.inc.glsl"
#include "vol_fog.inc.glsl"

layout (location = 0) in vec2 v_uv;
layout (binding = 1) uniform sampler2D u_depth;
layout (binding = 2) uniform sampler3D u_integrated;
layout (binding = 4, std430) readonly buffer GiGridData { float gi_gridData[]; };

// Terrain cascades: the far field runs the SAME terrain-following model as the froxel volume, marched.
#define TERRAIN_HEIGHT_BINDING 3
#include "terrain_height.inc.glsl"
#define GI_GRID_DATA_NAME gi_gridData
#include "gi_probe.inc.glsl"

layout (location = 0) out vec4 out_color;

// View index (0 = centre/desktop, 1 = left eye, 2 = right eye); selects the per-eye depth reconstruction.
// The fog volume itself is built once for the centre view, sampled here at each eye's reconstructed world pos.
layout (push_constant) uniform ViewPC { uint u_viewIndex; };

// The fog base (world Y) the medium hugs at worldXZ — the SAME terrain-follow datum vol_scatter builds per
// froxel (global height base + a fraction of the macro landscape altitude over sea level), so the near and
// far models are one model and agree at the seam by construction.
float volFarFieldBase(vec2 worldXZ, out vec4 data)
{
    data = terrainDataAt(worldXZ);
    return u_fogParams0.y + u_fogParams3.x * (u_fogParams5.w + max(data.w, 0.0));
}

// Analytic height fog over the ray segment [t0, t1] (t0 = the froxel volume's far plane), returned in the
// same (in-scatter, transmittance) form the integrated volume uses so the two just compose.
//
// Terrain-following, via a PIECEWISE-EXACT march: the ground is sampled at a handful of points along the
// ray, and between consecutive samples it is taken as linear — for which the height-fog integral is still
// exactly closed-form, since a linear ground just subtracts its slope from the ray's (see
// volAnalyticOpticalDepthLinear). So this is NOT a density raymarch:
//
//   * Nothing is being sampled and summed, so there is no integration error to quantize — it cannot band,
//     and it needs no temporal filtering or jitter to converge. Each sub-segment is exact.
//   * The total is C0 in t, so sub-segment boundaries are invisible. They are not fixed shells in view
//     space that sweep as the camera moves; they are just where the linear-ground approximation is
//     re-anchored, and the result is continuous across them.
//   * A small step count suffices, because the error is only in interpolating the GROUND between samples —
//     a smooth, km-scale field — not in the optical depth.
//
// Marching is also what makes terrain-following viable here at all. Every earlier attempt point-sampled the
// terrain at one place per ray and fed it into an exponential, which is why it produced horizon lines,
// pillars, radial streaks and whole-screen pulsing in turn. An integral ALONG the ray averages the terrain,
// and neighbouring pixels share nearly all of their path, so the result varies smoothly across the screen
// and moves with the world rather than with the view.
vec4 volFarField(vec3 dir, float t0, float t1)
{
    const float density = u_fogParams0.x * u_fogParams9.y;
    if (density <= 1e-7 || t1 <= t0)
        return vec4(0.0, 0.0, 0.0, 1.0);
    // The near field's own height falloff, scaled: same layer, optionally thicker at range.
    const float falloff = u_fogParams0.z * u_fogParams9.z;

    float tau;
    if (!terrainHeightMapPresent() || u_fogParams3.x <= 0.0)
    {
        // Flat base: the whole segment is one exact solve, no march needed.
        tau = volAnalyticOpticalDepth(u_viewPos, dir, t0, t1, u_fogParams0.y, falloff, density);
    }
    else
    {
        // March only as far as the cascades hold ground data; past that the map clamps to its edge, so the
        // remainder is a constant-ground tail and solves exactly in one step. This also keeps sky rays
        // (t1 = VOL_FAR_INFINITY) from spreading their steps uselessly across 10,000 km.
        const float reach = (u_fogParams5.z > 0.0) ? 0.5 / u_fogParams5.z : 0.5 / u_fogParams3.y;
        const float tEnd = min(t1, t0 + reach);
        const int steps = max(int(u_fogParams9.w), 1);

        vec4 dPrev;
        float basePrev = volFarFieldBase(u_viewPos.xz + dir.xz * t0, dPrev);
        float tPrev = t0;
        tau = 0.0;

        for (int i = 1; i <= steps; ++i)
        {
            const float tNext = mix(t0, tEnd, float(i) / float(steps));
            vec4 dNext;
            const float baseNext = volFarFieldBase(u_viewPos.xz + dir.xz * tNext, dNext);
            const float len = tNext - tPrev;

            // Regional fog fields, same as vol_scatter: thickness scales density, temperature sets how
            // hard the layer hugs the ground. Nearest-texel is fine HERE where a single sample was not —
            // the march integrates these, so a texel step contributes a fraction of one sub-segment
            // instead of setting the whole ray's density.
            float dens = density;
            float k = falloff;
            if (u_fogParams6.z > 0.0)
            {
                const vec2 midXZ = u_viewPos.xz + dir.xz * (0.5 * (tPrev + tNext));
                const vec4 climate = terrainClimateNearestAt(midXZ);
                dens *= mix(1.0, climate.x, u_fogParams6.z);
                k *= mix(1.0, fogFalloffFromTemperature(terrainTemperatureAt(climate, 0.5 * (dPrev.x + dNext.x))), u_fogParams6.z);
            }

            // Ground linear over this sub-segment => h linear => exact.
            tau += volAnalyticOpticalDepthLinear(u_viewPos.y + dir.y * tPrev - basePrev,
                                                 dir.y - (baseNext - basePrev) / max(len, 1e-3),
                                                 len, k, dens);
            if (tau > 12.0) // transmittance already < 1e-5; the rest cannot show
                break;

            tPrev = tNext;
            basePrev = baseNext;
            dPrev = dNext;
        }

        // Constant-ground tail out to t1 (the sky's infinity included), exact in one solve.
        if (t1 > tEnd && tau <= 12.0)
            tau += volAnalyticOpticalDepthLinear(u_viewPos.y + dir.y * tEnd - basePrev, dir.y, t1 - tEnd, falloff, density);
    }
    const float T = exp(-tau);
    if (T >= 0.9999)
        return vec4(0.0, 0.0, 0.0, 1.0);

    // With the lighting constant over the segment the single-scatter integral is exact and free: dtau/dt
    // IS the extinction, so integral(rho * exp(-tau)) collapses to (1 - T). No accumulation loop, and
    // nothing to converge — this is the closed form, not an approximation of one.
    // Unshadowed sun. The froxel volume's terrain sun march is deliberately NOT used here: it is a sparse
    // 10-tap min over the height cascades, which is fine for a filtered, temporally blended froxel grid but
    // per-pixel becomes another view-locked field that streaks (see the sampling note above). The HG phase
    // already carries the directional cue that reads — far haze brightening toward the sun.
    const vec3 sunDir = normalize(u_sunDirection.xyz);
    vec3 inLight = atmosTransmittanceToLight(0.0, sunDir, u_skyUp) * u_sunColor.rgb
        * (volPhaseHG(dot(dir, sunDir), u_fogParams1.w) * u_eclipseParams.x);
    // Ambient is the virtual sky probe only: the GI probe field ends well inside the froxel volume, so
    // out here evalProbeSHCoverage would report zero coverage and hand over to exactly this anyway.
    inLight += giEvalSkySH(-dir) * u_aoParams.y / PI + u_ambientColor;

    return vec4(u_fogParams1.rgb * inLight * (1.0 - T), T);
}

void main()
{
    g_viewIndex = int(u_viewIndex);
    const float depth = texture(u_depth, v_uv).r;
    // Reconstruct this eye's world position (current view), then reproject it through the shared CENTRE view
    // that the froxel volume was built in, so the lookup lands at the correct froxel regardless of which eye
    // is sampling (on desktop the centre view IS this view, so vpUv collapses back to v_uv). Sampling the
    // centre-built volume at the eye's own screen UV would offset the fog by the eye/head parallax.
    const vec3 worldPos = worldPosFromDepth(v_uv, depth);
    const vec4 centerClip = u_views[VIEW_CENTER].mvp * vec4(worldPos, 1.0);
    const float viewZ = centerClip.w;
    const vec2 ndc = centerClip.xy / centerClip.w;
    const vec2 vpUv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    // Outside the centre view (a sliver an eye can see past the volume's coverage): emit no fog rather than
    // letting the clamp sampler smear the edge froxels across the screen.
    if (centerClip.w <= 0.0 || any(lessThan(vpUv, vec2(0.0))) || any(greaterThan(vpUv, vec2(1.0))))
    {
        out_color = vec4(0.0, 0.0, 0.0, 1.0); // inScatter 0, transmittance 1 -> scene unchanged
        return;
    }
    // Texel z of the integrated volume stores the fog state at slice z's FAR edge, so the matching
    // texture coordinate sits half a slice below the continuous slice coordinate (texel centers are at
    // +0.5). Sampling without that shift read fog from half a slice too deep and anchored the linear
    // reconstruction between slices wrong — one source of view-aligned fog banding.
    const float s = volViewZToSlice(viewZ) * float(VOL_FROXEL_Z); // continuous slice index
    // Per-pixel interleaved-gradient dither (+-half a slice, rotated per frame): breaks the residual
    // Mach banding of the piecewise-linear reconstruction into noise below TAA's threshold.
    const vec2 px = gl_FragCoord.xy + 5.588238 * float(u_frameIndex & 63u);
    const float ign = fract(52.9829189 * fract(0.06711056 * px.x + 0.00583715 * px.y));
    const float w = clamp((s - 0.5 + (ign - 0.5)) / float(VOL_FROXEL_Z), 0.0, 1.0);
    vec4 fog = texture(u_integrated, vec3(vpUv, w));
    // Inside the first slice there is no "before fog" texel to lerp from: fade the fog in linearly so
    // very near geometry isn't over-fogged by the first slice's full accumulation.
    if (s < 1.0)
        fog = mix(vec4(0.0, 0.0, 0.0, 1.0), fog, max(s, 0.0));

    // ---- Analytic far field, beyond the volume's far plane ---------------------------------------------
    // The seam needs no matching work: both terms are the same height-fog profile, and the far one is
    // defined as an integral BETWEEN two distances, so it simply starts where the volume stops. Continuity
    // is by construction as long as the density parameters agree (they are the same UBO fields).
    if (u_fogParams9.x > 0.5)
    {
        // View ray from u_mvp's x/y/w ROWS — NOT from the reconstructed world position. u_invMvp is a
        // float32 CPU inverse whose error grows with the camera's distance from the origin and RE-ROLLS
        // EVERY FRAME, so any ray built through it wobbles per frame. For a sky pixel the far field is a
        // pure function of this ray (the segment runs to infinity), so that wobble IS flicker, and TAA
        // cannot filter it: the resolved colour genuinely differs every frame. Solving {r0.d = ndc.x,
        // r1.d = ndc.y, rw.d = 1} instead gives the exact ray from O(1)-magnitude terms, with no camera
        // translation in it. Same derivation as sky.fs.glsl and vol_scatter.cs.glsl.
        //
        // u_mvp is unjittered while the DEPTH IMAGE was rasterized jittered, so back the jitter out of the
        // NDC (the -u_taaJitter.xy here is exactly the ndc form of shared.inc.glsl's taaJitterUv
        // subtraction): that both matches the depth sample this pixel actually read and leaves the ray
        // frame-invariant, so a static camera resolves the far fog to the identical value every frame.
        const vec2 vpUv = (v_uv - u_viewportRect.xy) / u_viewportRect.zw;
        const vec2 rayNdc = vec2(vpUv.x * 2.0 - 1.0, 1.0 - vpUv.y * 2.0) - u_taaJitter.xy;
        const mat3 rayFromNdc = inverse(mat3(
            vec3(u_mvp[0][0], u_mvp[1][0], u_mvp[2][0]),
            vec3(u_mvp[0][1], u_mvp[1][1], u_mvp[2][1]),
            vec3(u_mvp[0][3], u_mvp[1][3], u_mvp[2][3])));
        const vec3 dir = normalize(vec3(rayNdc, 1.0) * rayFromNdc);
        const vec3 camFwd = normalize(vec3(0.0, 0.0, 1.0) * rayFromNdc);
        // The volume is bounded by view-Z but the closed form integrates along the ray, so convert.
        const float invCos = 1.0 / max(dot(dir, camFwd), 1e-3);
        const float t0 = volFogFar() * invCos;
        // Sky rays run to infinity rather than to the far plane, which is the whole point of an unbounded
        // far field: a level ray saturates to full fog at the horizon, an upward one converges on a finite
        // optical depth and stays see-through. Both limits fall out of the closed form on their own.
        const float t1 = (depth <= 0.0) ? VOL_FAR_INFINITY : viewZ * invCos;
        if (t1 > t0)
        {
            const vec4 far = volFarField(dir, t0, t1);
            fog = vec4(fog.rgb + fog.a * far.rgb, fog.a * far.a);
        }
    }
    out_color = vec4(fog.rgb, fog.a);
}
