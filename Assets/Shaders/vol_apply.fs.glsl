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
    const float w = clamp(volViewZToSlice(viewZ), 0.0, 1.0);
    const vec4 fog = texture(u_integrated, vec3(vpUv, w));
    out_color = vec4(fog.rgb, fog.a);
}
