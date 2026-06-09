#version 460

#extension GL_GOOGLE_include_directive : enable

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "ubo.inc.glsl"

layout (binding = 1) uniform sampler2D u_curDepth;   // this frame's hardware depth (full-res gbuffer)
layout (binding = 2) uniform sampler2D u_prevDepth;  // previous frame's hardware depth
layout (binding = 3) uniform sampler2D u_rawAO;      // this frame's raw AO (rgb = bent normal, a = AO, half-res)
layout (binding = 4) uniform sampler2D u_historyAO;  // previous frame's accumulated AO (rgb = bent normal, a = AO)
layout (binding = 5, rgba16f) uniform restrict writeonly image2D u_accumOut;

layout (push_constant) uniform PC
{
    uint  aoWidth;
    uint  aoHeight;
    float maxHistory;   // blend weight for valid history (e.g. 0.92)
    float _pad;
} pc;

void main()
{
    const ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= int(pc.aoWidth) || px.y >= int(pc.aoHeight))
        return;

    const vec2 uv = (vec2(px) + 0.5) / vec2(pc.aoWidth, pc.aoHeight);
    const vec4 raw = texture(u_rawAO, uv); // rgb = bent normal, a = AO
    const float depth = texture(u_curDepth, uv).r;
    if (depth >= 1.0) { imageStore(u_accumOut, px, raw); return; }

    const vec3 worldPos = worldPosFromDepth(uv, depth);

    float clipW;
    const vec2 prevUv = prevScreenUV(worldPos, clipW);

    float histWeight = 0.0;
    vec4 hist = raw;
    if (clipW > 0.0 && all(greaterThanEqual(prevUv, vec2(0.0))) && all(lessThanEqual(prevUv, vec2(1.0))))
    {
        const float prevDepth = texture(u_prevDepth, prevUv).r;
        if (prevDepth < 1.0)
        {
            const vec3 prevWorld = worldPosFromDepthMat(prevUv, prevDepth, u_prevInvMvp);
            // Distance-scaled disocclusion threshold: tolerate more at range (depth precision falls off).
            const float viewDist = length(worldPos - u_viewPos);
            const float thresh = 0.02 + 0.01 * viewDist;
            if (length(prevWorld - worldPos) < thresh)
            {
                hist = texture(u_historyAO, prevUv);
                histWeight = pc.maxHistory;
            }
        }
    }

    vec4 accum = mix(raw, hist, histWeight); // AO in .a; bent normal in .xyz blends as a vector
    accum.xyz = (dot(accum.xyz, accum.xyz) > 1e-8) ? normalize(accum.xyz) : raw.xyz;
    imageStore(u_accumOut, px, accum);
}
