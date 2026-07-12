#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "shared.inc.glsl"
#define OCEAN_MAPS_BINDING 3
#define OCEAN_SHORE_BINDING 4
#define TERRAIN_HEIGHT_BINDING 5
#include "ocean_wave.inc.glsl"

// View index (0 = centre/desktop, 1 = left eye, 2 = right eye). The G-buffer is rendered once per eye into
// its own layer, like the forward pass; selects the matching view. No TAA jitter (this is the reference depth).
layout (push_constant) uniform ViewPC { uint u_viewIndex; };

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
layout (location = 3) in vec2 in_uv;
layout (location = 4) in uint inst_idx;

layout (location = 0) out vec3 out_normal; // world-space geometric normal
layout (location = 1) out vec2 out_uv;
layout (location = 2) out flat uint out_meshIdxMaterialIdx; // for the fragment alpha-mask discard

vec3 quat_transform(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
    g_viewIndex = int(u_viewIndex);
    const InMeshInstancesData inst = in_instances[inst_idx];
    // Sky sphere: emit degenerate triangles so it never writes the G-buffer. Its depth stays at the
    // cleared far plane, so TAA reprojection (and fog/AO depth reads) treat the sky as infinitely far
    // instead of as a finite camera-anchored sphere, which reprojected with parallax and blurred it.
    if ((in_materialInfos[inst.meshIdxMaterialIdx >> 16].flags & MATERIAL_FLAG_SKY) != 0u)
    {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0); // behind the far plane; clipped
        out_normal = vec3(0.0);
        out_uv = vec2(0.0);
        out_meshIdxMaterialIdx = 0u;
        return;
    }
    vec3 worldPos = quat_transform(in_pos * inst.posScale.w, inst.quat) + inst.posScale.xyz;
    out_normal = quat_transform(in_normal, inst.quat);
    // Ocean water: displace with the exact same clipmap morph + FFT displacement-map samples the forward
    // Ocean variant uses, so this reference depth/normal — which TAA reprojection and RTAO read — tracks
    // the drawn waves. The texcoord carries (ring cell size, morph weight); see instanced_indirect.vs.
    if ((in_materialInfos[inst.meshIdxMaterialIdx >> 16].flags & MATERIAL_FLAG_OCEAN) != 0u)
    {
        // Morph in MESH-LOCAL space (exact lattice) then re-transform — identical math to the forward
        // OCEAN vertex shader (instanced_indirect.vs.glsl), see the comment there.
        const float ringCell = in_uv.x;
        const float ringMorph = in_uv.y;
        vec3 localPos = in_pos;
        localPos.xz = mix(localPos.xz, floor(localPos.xz / (2.0 * ringCell) + 0.5) * (2.0 * ringCell), ringMorph);
        worldPos = quat_transform(localPos * inst.posScale.w, inst.quat) + inst.posScale.xyz;
        const vec2 baseXZ = worldPos.xz;
        if (oceanVertexCulled(baseXZ, ringCell))
        {
            // Buried under land: cull the primitives — identical test to the forward OCEAN vertex
            // shader, so the prepass depth and the drawn geometry stay in lockstep.
            out_normal = vec3(0.0, 1.0, 0.0);
            out_uv = in_uv;
            out_meshIdxMaterialIdx = inst.meshIdxMaterialIdx;
            gl_Position = vec4(uintBitsToFloat(0x7FC00000u));
            return;
        }
        worldPos.y += oceanWaterOffset(baseXZ); // water-table lift — identical to the forward pass
        worldPos += oceanSampleDisplacement(baseXZ, ringCell, ringMorph);
        out_normal = oceanSampleNormalLod(baseXZ, ringCell, ringMorph);
    }
    out_uv = in_uv;
    out_meshIdxMaterialIdx = inst.meshIdxMaterialIdx;
    gl_Position = u_mvp * vec4(worldPos, 1.0);
}
