#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

#include "shared.inc.glsl"

// Sun shadow caster cull. Tests each instance against every cascade's orthographic frustum and emits
// it into a single draw list (built like the camera cull) when it overlaps at least one cascade. The
// set of overlapping cascades is packed into a bitmask stored in the out-instance's meshIdxMaterialIdx
// slot (unused by the depth-only vertex shader); the multiview depth pass uses it to skip the
// cascades a caster does not touch. pipelineIndex is forced to 0 (single depth pipeline).

struct RenderNodeTransform  { vec4 posScale; vec4 quat; };
struct InMeshInstance       { uint renderNodeIdx; uint instanceOffsetIdx; uint meshIdxMaterialIdx; uint pipelineIdxAlphaMode; };
struct InMeshInstanceOffset { vec4 posScale; vec4 quat; };
struct InMeshInfo           { vec3 center; float radius; uint indexCount; uint firstIndex; int vertexOffset; uint _padding; };
struct MaterialInfo         { uint flags; float opacity; uint diffuseNormalTexIdx; uint metalRoughnessTexIdxAlphaMode; };
// alphaTexIdxCascadeMask: high 16 = alpha-mask texture index (0xFFFF when the material has no mask),
// low 16 = cascade overlap bitmask. Matches RendererVKLayout::OutShadowMeshInstance.
struct OutMeshInstance      { vec4 posScale; vec4 quat; uint alphaTexIdxCascadeMask; };
struct OutIndirectCommand   { uint pipelineIndex; uint indexCount; uint instanceCount; uint firstIndex; int vertexOffset; uint firstInstance; };

#include "ubo.inc.glsl"
layout (binding = 1, std430) readonly buffer InRenderNodeTransformsBuffer  { RenderNodeTransform  in_renderNodeTransforms[]; };
layout (binding = 2, std430) readonly buffer InMeshInstancesBuffer         { InMeshInstance       in_instances[]; };
layout (binding = 3, std430) readonly buffer InMeshInstanceOffsetsBuffer   { InMeshInstanceOffset in_instanceOffsets[]; };
layout (binding = 4, std430) readonly buffer InMeshInfoBuffer              { InMeshInfo           in_meshInfos[]; };
layout (binding = 5, std430) readonly buffer InFirstInstancesBuffer        { uint                 in_firstInstances[]; };
layout (binding = 6, std430) writeonly buffer OutMeshInstancesBuffer       { OutMeshInstance      out_meshInstances[]; };
layout (binding = 7, std430) writeonly buffer OutMeshInstanceIndexesBuffer { uint                 out_meshInstanceIndexes[]; };
layout (binding = 8, std430) writeonly buffer OutIndirectCommandBuffer     { OutIndirectCommand   out_indirectCommands[]; };
layout (binding = 9, std430) readonly buffer InMaterialInfos               { MaterialInfo         in_materialInfos[]; };

vec3 quat_transform(vec3 v, vec4 q) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }
vec4 quat_multiply(vec4 q, vec4 p)
{
    vec4 c, r;
    c.xyz = cross(q.xyz, p.xyz);
    c.w = -dot(q.xyz, p.xyz);
    r = p * q.w + c;
    r.xyz = (q * p.w + r).xyz;
    return r;
}

// Builds a cascade bitmask: bit c set if the world-space sphere overlaps cascade c's clip volume
// (Vulkan zero-to-one depth: -w<=x,y<=w and 0<=z<=w), extracted from each cascade's view-projection.
uint cascadeOverlapMask(vec3 center, float radius)
{
    uint mask = 0u;
    for (uint c = 0; c < NUM_SHADOW_CASCADES; ++c)
    {
        mat4 m = u_cascadeViewProj[c];
        // Gribb-Hartmann planes; rows of the column-major matrix.
        vec4 rx = vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
        vec4 ry = vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
        vec4 rz = vec4(m[0][2], m[1][2], m[2][2], m[3][2]);
        vec4 rw = vec4(0.0, 0.0, 0.0, 1.0); // true bottom row; the matrix slots hold packed per-cascade scalars
        vec4 planes[6] = vec4[6](rw + rx, rw - rx, rw + ry, rw - ry, rz, rw - rz); // last two are ZO near/far
        bool inside = true;
        for (uint p = 0; p < 6; ++p)
        {
            vec4 pl = planes[p];
            float invLen = inversesqrt(max(dot(pl.xyz, pl.xyz), 1e-12));
            if (dot(pl.xyz, center) * invLen + pl.w * invLen < -radius)
            {
                inside = false;
                break;
            }
        }
        if (inside)
            mask |= (1u << c);
    }
    return mask;
}

void main()
{
    const uint instanceIdx        = gl_GlobalInvocationID.x;
    const InMeshInstance instance = in_instances[instanceIdx];
    const uint16_t meshIdx        = uint16_t(instance.meshIdxMaterialIdx & 0x0000FFFF);
    const InMeshInfo meshInfo     = in_meshInfos[meshIdx];

    const vec4 quat                   = quat_multiply(in_renderNodeTransforms[instance.renderNodeIdx].quat, in_instanceOffsets[instance.instanceOffsetIdx].quat);
    const vec4 renderNodePosScale     = in_renderNodeTransforms[instance.renderNodeIdx].posScale;
    const vec4 instanceOffsetPosScale = in_instanceOffsets[instance.instanceOffsetIdx].posScale;
    const vec4 instancePosScale       = vec4(renderNodePosScale.xyz + quat_transform(instanceOffsetPosScale.xyz, in_renderNodeTransforms[instance.renderNodeIdx].quat),
                                            renderNodePosScale.w * instanceOffsetPosScale.w);
    const vec3 centerOffset           = quat_transform(meshInfo.center * instancePosScale.w, quat);
    const float radius                = meshInfo.radius * instancePosScale.w;
    const vec3 centerPos              = instancePosScale.xyz + centerOffset;

    const uint cascadeMask = cascadeOverlapMask(centerPos, radius);
    if (cascadeMask == 0u)
        return; // casts no shadow in any cascade

    // Resolve the alpha-mask texture once here (0xFFFF = opaque) so the depth pass can discard cutout
    // fragments without touching the material buffer.
    const uint materialIdx = (instance.meshIdxMaterialIdx & 0xFFFF0000u) >> 16;
    const MaterialInfo material = in_materialInfos[materialIdx];
    const uint alphaMode = (material.metalRoughnessTexIdxAlphaMode & 0xFFFF0000u) >> 16;
    const uint alphaTexIdx = (alphaMode == ALPHA_MODE_MASK) ? (material.diffuseNormalTexIdx & 0x0000FFFFu) : 0xFFFFu;
    const uint packed = (alphaTexIdx << 16) | (cascadeMask & 0x0000FFFFu);

    const uint firstInstance = in_firstInstances[meshIdx];
    const uint cmdIdx = meshIdx; // single opaque region
    const uint idx = atomicAdd(out_indirectCommands[cmdIdx].instanceCount, 1);
    if (idx == 0)
    {
        out_indirectCommands[cmdIdx].pipelineIndex = 0u;
        out_indirectCommands[cmdIdx].indexCount    = meshInfo.indexCount;
        out_indirectCommands[cmdIdx].firstIndex    = meshInfo.firstIndex;
        out_indirectCommands[cmdIdx].vertexOffset  = meshInfo.vertexOffset;
        out_indirectCommands[cmdIdx].firstInstance = firstInstance;
    }

    out_meshInstanceIndexes[firstInstance + idx]           = instanceIdx;
    out_meshInstances[instanceIdx].posScale                = instancePosScale;
    out_meshInstances[instanceIdx].quat                    = quat;
    out_meshInstances[instanceIdx].alphaTexIdxCascadeMask  = packed;
}
