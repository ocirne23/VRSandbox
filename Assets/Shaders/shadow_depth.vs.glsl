#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_multiview : require

#include "shared.inc.glsl"

// Depth-only vertex shader for the sun shadow cascades, rendered in a single multiview pass:
// gl_ViewIndex selects the cascade. The shadow cull packed the set of cascades each caster overlaps
// into a bitmask (in the meshIdxMaterialIdx slot); for cascades a caster does not touch the primitive
// is collapsed to a degenerate point so it is cheaply discarded before rasterization.
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
struct InMeshInstancesData
{
    vec4 posScale;
    vec4 quat;
    uint cascadeMask; // bit c set if this caster overlaps cascade c
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
    if ((inst.cascadeMask & (1u << gl_ViewIndex)) == 0u)
    {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0); // not in this cascade: degenerate -> discarded
        return;
    }
    vec3 worldPos = quat_transform(in_pos * inst.posScale.w, inst.quat) + inst.posScale.xyz;
    gl_Position = u_cascadeViewProj[gl_ViewIndex] * vec4(worldPos, 1.0);
}
