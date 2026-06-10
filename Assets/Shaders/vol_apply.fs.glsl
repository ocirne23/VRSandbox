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

void main()
{
    const float depth = texture(u_depth, v_uv).r;
    const vec3 worldPos = worldPosFromDepth(v_uv, depth);
    const float viewZ = (u_mvp * vec4(worldPos, 1.0)).w;

    const vec2 vpUv = (v_uv - u_viewportRect.xy) / u_viewportRect.zw;
    const float w = clamp(volViewZToSlice(viewZ), 0.0, 1.0);
    const vec4 fog = texture(u_integrated, vec3(vpUv, w));
    out_color = vec4(fog.rgb, fog.a);
}
