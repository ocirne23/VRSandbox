#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "shared.inc.glsl"

// Depth-only vertex shader for the sun shadow cascades. The cascade view-projections live in the
// (staging-uploaded) main UBO; a push constant selects which cascade this draw renders into, so no
// per-cascade host-written uniform buffer is needed (which would race across frames in flight).
layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
    vec4 u_sunDirection;
    vec4 u_sunColor;
    mat4 u_cascadeViewProj[NUM_SHADOW_CASCADES];
    vec4 u_cascadeSplits;
    vec4 u_shadowParams;
};
layout (push_constant) uniform PushConstants
{
    uint u_cascadeIndex;
};
struct InMeshInstancesData
{
    vec4 posScale;
    vec4 quat;
    uint meshIdxMaterialIdx;
};
layout (binding = 1, std430) readonly buffer InMeshInstances
{
    InMeshInstancesData in_instances[];
};

layout (location = 0) in vec3 in_pos;
layout (location = 4) in uint inst_idx;

vec3 quat_transform(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
    const InMeshInstancesData inst = in_instances[inst_idx];
    vec3 worldPos = quat_transform(in_pos * inst.posScale.w, inst.quat) + inst.posScale.xyz;
    gl_Position = u_cascadeViewProj[u_cascadeIndex] * vec4(worldPos, 1.0);
}
