#version 450

// Volumetric fog apply: fullscreen pass in the scene-color render pass (after the forward + sky draws,
// before TAA so the fog gets antialiased). Reconstructs each pixel's view depth from the G-buffer depth
// (sky pixels stay at the far plane and receive the full fog range) and samples the integrated fog volume.
// Blended with (srcColor = ONE, dstColor = SRC_ALPHA): out = inScatter + sceneColor * transmittance.

#include "shared.inc.glsl"
#include "vol_fog.inc.glsl"

layout (location = 0) in vec2 v_uv;
layout (binding = 1) uniform sampler2D u_depth;
layout (binding = 2) uniform sampler3D u_integrated;
layout (location = 0) out vec4 out_color;

// View index (0 = centre/desktop, 1 = left eye, 2 = right eye); selects the per-eye depth reconstruction.
// The fog volume itself is built once for the centre view, sampled here at each eye's reconstructed world pos.
layout (push_constant) uniform ViewPC { uint u_viewIndex; };

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
    out_color = vec4(fog.rgb, fog.a);
}
