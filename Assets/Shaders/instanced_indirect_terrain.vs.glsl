#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Terrain variant of instanced_indirect.vs.glsl. The terrain fragment shader needs only world position and
// the geometric normal (it builds its own tangent bases for XZ/triplanar splatting), so this VS skips the
// tangent/bitangent (no full TBN) and the UV entirely — 6 interpolant components instead of 15. The shared
// vertex input layout still describes tangent (loc 2) and uv (loc 3); leaving them unconsumed is valid.

#include "shared.inc.glsl"

#ifdef STEREO
layout (push_constant) uniform ViewPC { uint u_viewIndex; };
#endif

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
layout (location = 1) in vec3 in_normal;
layout (location = 4) in uint inst_idx;

layout (location = 0) out vec3 out_pos;
layout (location = 1) out vec3 out_normal;

vec3 quat_transform(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
#ifdef STEREO
    g_viewIndex = int(u_viewIndex);
#endif
    const InMeshInstancesData inst = in_instances[inst_idx];

    out_normal = quat_transform(in_normal, inst.quat);
    out_pos    = quat_transform(in_pos * inst.posScale.w, inst.quat) + inst.posScale.xyz;

    gl_Position = u_mvp * vec4(out_pos, 1.0);
    gl_Position.xy += u_taaJitter.xy * gl_Position.w; // TAA sub-pixel jitter (clip space)
}
