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

#define NUM_SHADOW_CASCADES 6
#include "ubo.inc.glsl"

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
    float _pad;
} pc;

bool insideViewport(vec2 uv)
{
    return all(greaterThanEqual(uv, u_viewportRect.xy)) &&
           all(lessThanEqual(uv, u_viewportRect.xy + u_viewportRect.zw));
}

void main()
{
    const ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.width) || px.y >= int(pc.height))
        return;

    const vec2 texel = 1.0 / vec2(pc.width, pc.height);
    const vec2 uv = (vec2(px) + 0.5) * texel;

    const vec3 current = texture(u_currentColor, uv).rgb;
    const float depth = texture(u_gbufferDepth, uv).r;

    // Sky/background (no geometry) or pixels outside the editor viewport panel carry no reliable motion;
    // pass the current sample straight through.
    if (depth >= 1.0 || !insideViewport(uv) || pc.feedback <= 0.0)
    {
        imageStore(u_resolveOut, px, vec4(current, 1.0));
        return;
    }

    const vec3 worldPos = worldPosFromDepth(uv, depth);
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
    if (prevDepth < 1.0)
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
