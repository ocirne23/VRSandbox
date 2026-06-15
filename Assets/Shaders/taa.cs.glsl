#version 460

#extension GL_GOOGLE_include_directive : enable

// Temporal anti-aliasing resolve. One full-resolution compute pass per frame:
//   - reconstruct this pixel's world position from the (jittered) camera depth,
//   - reproject it into last frame's screen via u_prevMvp to fetch the history color,
//   - clamp the history to the current 3x3 neighborhood colour box (variance clipping) to suppress ghosting,
//   - reject history on disocclusion (reprojected depth mismatch) or when it lands outside the viewport,
//   - blend current and (clamped) history with a fixed feedback weight.
// The camera projection is jittered by a sub-pixel offset each frame (see Renderer::beginFrame); accumulating
// the jittered samples over time is what yields the anti-aliasing. Resolve happens in the scene-colour space
// (swapchain format), so this is an LDR resolve. The result is both the displayed image (composited into the
// swapchain) and next frame's history (ping-ponged per frame-in-flight, like the RTAO/GI buffers).

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "shared.inc.glsl"

layout (binding = 1) uniform sampler2D u_currentColor;     // this frame's scene colour (jittered render)
layout (binding = 2) uniform sampler2D u_historyColor;     // last frame's resolved colour (reprojected)
layout (binding = 3) uniform sampler2D u_gbufferDepth;     // this frame's camera depth
layout (binding = 4) uniform sampler2D u_prevGbufferDepth; // last frame's camera depth (disocclusion test)
layout (binding = 5, rgba16f) uniform restrict writeonly image2D u_resolveOut;

layout (push_constant) uniform PC
{
    uint  width;
    uint  height;
    float feedback;  // history weight in [0,1]; 0 disables temporal accumulation
    uint  viewIndex; // view to reconstruct in (0 = centre/desktop, 1 = left eye, 2 = right eye)
} pc;

bool insideViewport(vec2 uv)
{
    return all(greaterThanEqual(uv, u_viewportRect.xy)) &&
           all(lessThanEqual(uv, u_viewportRect.xy + u_viewportRect.zw));
}

void main()
{
    g_viewIndex = int(pc.viewIndex);
    const ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.width) || px.y >= int(pc.height))
        return;

    const vec2 texel = 1.0 / vec2(pc.width, pc.height);
    const vec2 uv = (vec2(px) + 0.5) * texel;

    const vec3 current = texture(u_currentColor, uv).rgb;
    const float depth = texture(u_gbufferDepth, uv).r;

    // Pixels outside the editor viewport panel carry no reliable motion; pass straight through.
    if (!insideViewport(uv) || pc.feedback <= 0.0)
    {
        imageStore(u_resolveOut, px, vec4(current, 1.0));
        return;
    }

    // Sky pixels (the sky sphere is excluded from the G-buffer, so its depth stays at the far plane)
    // reproject AT the far plane: the reprojection is then purely rotational (parallax-free, correct for
    // content at infinity) and the jittered sky raster still accumulates instead of visibly shaking.
    const bool sky = depth >= 1.0;
    const vec3 worldPos = worldPosFromDepth(uv, min(depth, 1.0));
    float clipW;
    const vec2 prevUv = prevScreenUV(worldPos, clipW);

    const bool histValid = clipW > 0.0 && insideViewport(prevUv);
    if (!histValid)
    {
        imageStore(u_resolveOut, px, vec4(current, 1.0));
        return;
    }

    // 3x3 current-colour statistics for the variance-clipping box.
    vec3 nmin = current, nmax = current, m1 = vec3(0.0), m2 = vec3(0.0);
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        const vec3 c = texture(u_currentColor, uv + vec2(dx, dy) * texel).rgb;
        nmin = min(nmin, c);
        nmax = max(nmax, c);
        m1 += c;
        m2 += c * c;
    }
    const vec3 mean  = m1 / 9.0;
    const vec3 sigma = sqrt(max(m2 / 9.0 - mean * mean, vec3(0.0)));
    const vec3 cmin  = max(nmin, mean - 1.25 * sigma);
    const vec3 cmax  = min(nmax, mean + 1.25 * sigma);

    vec3 history = texture(u_historyColor, prevUv).rgb;
    history = clamp(history, cmin, cmax);

    // Disocclusion: reconstruct last frame's world position at the reprojected pixel and reject the history
    // if the surface moved more than a view-distance-scaled threshold (something else was there last frame).
    const float prevDepth = texture(u_prevGbufferDepth, prevUv).r;
    float fb = pc.feedback;
    if (sky)
    {
        // Sky accumulates against sky only; geometry there last frame means a disocclusion.
        if (prevDepth < 1.0)
            fb = 0.0;
    }
    else if (prevDepth < 1.0)
    {
        const vec3 prevWorld = worldPosFromDepthMat(prevUv, prevDepth, u_prevInvMvp);
        const float thresh = 0.05 * (1.0 + distance(u_viewPos, worldPos));
        if (distance(prevWorld, worldPos) > thresh)
            fb = 0.0;
    }
    else
    {
        fb = 0.0; // last frame had no geometry here -> nothing to accumulate
    }

    const vec3 result = mix(current, history, fb);
    imageStore(u_resolveOut, px, vec4(result, 1.0));
}
