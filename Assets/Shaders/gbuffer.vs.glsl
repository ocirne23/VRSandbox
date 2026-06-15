#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "shared.inc.glsl"

// Eye index (0 on desktop / left eye). The G-buffer is rendered once per eye into its own layer, like the
// forward pass; selects the matching per-eye projection. No TAA jitter here (this is the reference depth).
layout (push_constant) uniform EyePC { uint u_eyeIndex; };

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
struct MaterialInfo
{
    uint flags;
    float opacity;
    uint diffuseNormalTexIdx;
    uint metalRoughnessTexIdxAlphaMode;
};
layout (binding = 2, std430) readonly buffer InMaterials { MaterialInfo in_materialInfos[]; };

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec3 in_normal;
layout (location = 4) in uint inst_idx;

layout (location = 0) out vec3 out_normal; // world-space geometric normal

vec3 quat_transform(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
    const InMeshInstancesData inst = in_instances[inst_idx];
    // Sky sphere: emit degenerate triangles so it never writes the G-buffer. Its depth stays at the
    // cleared far plane, so TAA reprojection (and fog/AO depth reads) treat the sky as infinitely far
    // instead of as a finite camera-anchored sphere, which reprojected with parallax and blurred it.
    if ((in_materialInfos[inst.meshIdxMaterialIdx >> 16].flags & MATERIAL_FLAG_SKY) != 0u)
    {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0); // behind the far plane; clipped
        out_normal = vec3(0.0);
        return;
    }
    vec3 worldPos = quat_transform(in_pos * inst.posScale.w, inst.quat) + inst.posScale.xyz;
    out_normal = quat_transform(in_normal, inst.quat);
    gl_Position = u_mvpStereo[u_eyeIndex] * vec4(worldPos, 1.0);
}
