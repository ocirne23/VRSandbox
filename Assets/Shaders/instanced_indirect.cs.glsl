#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
//#extension GL_EXT_debug_printf : enable

#include "shared.inc.glsl"

struct RenderNodeTransform
{
    vec4 posScale;
    vec4 quat;
};
struct InMeshInstance
{
    uint renderNodeIdx;
    uint instanceOffsetIdx;
    uint meshIdxMaterialIdx;
    uint pipelineIdxAlphaMode;
};
struct InMeshInstanceOffset
{
    vec4 posScale;
    vec4 quat;
};
struct InMeshInfo
{
    vec3 center;
    float radius;
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint _padding;
};
struct OutMeshInstance
{
    vec4 posScale;
    vec4 quat;
    uint meshIdxMaterialIdx;
    uint _padding1;
    uint _padding2;
    uint _padding3;
};
// Matches RendererVKLayout::IndirectDrawSequence: an EXECUTION_SET pipelineIndex followed by a
// VkDrawIndexedIndirectCommand. Consumed by vkCmdExecuteGeneratedCommandsEXT.
struct OutIndirectCommand
{
    uint pipelineIndex;
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

layout (binding = 1, std430) readonly buffer InRenderNodeTransformsBuffer
{
    RenderNodeTransform in_renderNodeTransforms[];
};
layout (binding = 2, std430) readonly buffer InMeshInstancesBuffer
{
    InMeshInstance in_instances[];
};
layout (binding = 3, std430) readonly buffer InMeshInstanceOffsetsBuffer
{
    InMeshInstanceOffset in_instanceOffsets[];
};
layout (binding = 4, std430) readonly buffer InMeshInfoBuffer
{
    InMeshInfo in_meshInfos[];
};
layout (binding = 5, std430) readonly buffer InFirstInstancesBuffer
{
    uint in_firstInstances[];
};
layout (binding = 6, std430) writeonly buffer OutMeshInstancesBuffer
{
    OutMeshInstance out_meshInstances[];
};
layout (binding = 7, std430) writeonly buffer OutMeshInstanceIndexesBuffer
{
    uint out_meshInstanceIndexes[];
};

layout (binding = 8, std430) writeonly buffer OutIndirectCommandBuffer
{
    OutIndirectCommand out_indirectCommands[]; // opaque
};

layout (binding = 9, std430) writeonly buffer OutTransparentIndirectCommandBuffer
{
    OutIndirectCommand out_transparentIndirectCommands[];
};

vec3 quat_transform(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

vec4 quat_multiply(vec4 q, vec4 p)
{
    vec4 c, r;
    c.xyz = cross(q.xyz, p.xyz);
    c.w = -dot(q.xyz, p.xyz);
    r = p * q.w + c;
    r.xyz = (q * p.w + r).xyz;
    return r;
}

bool frustumCheck(vec3 pos, float radius)
{
    // Check sphere against frustum planes
    for (int i = 0; i < 6; i++) 
    {
        if (dot(vec4(pos, 1.0), u_frustumPlanes[i]) + radius < 0.0)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    //debugPrintfEXT("instanceIdx %d\n", gl_GlobalInvocationID.x);
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

    if (frustumCheck(centerPos, radius))
    {
        const uint firstInstance      = in_firstInstances[meshIdx];
        const uint16_t pipelineIdx    = uint16_t(instance.pipelineIdxAlphaMode & 0x0000FFFF);
        const uint16_t alphaMode      = uint16_t((instance.pipelineIdxAlphaMode & 0xFFFF0000) >> 16);
        const bool isTransparent      = alphaMode == ALPHA_MODE_BLEND;

        uint idx;
        if (isTransparent)
        {
            idx = atomicAdd(out_transparentIndirectCommands[meshIdx].instanceCount, 1);
            if (idx == 0)
            {
                out_transparentIndirectCommands[meshIdx].pipelineIndex = pipelineIdx;
                out_transparentIndirectCommands[meshIdx].indexCount    = meshInfo.indexCount;
                out_transparentIndirectCommands[meshIdx].firstIndex    = meshInfo.firstIndex;
                out_transparentIndirectCommands[meshIdx].vertexOffset  = meshInfo.vertexOffset;
                out_transparentIndirectCommands[meshIdx].firstInstance = firstInstance;
            }
        }
        else
        {
            idx = atomicAdd(out_indirectCommands[meshIdx].instanceCount, 1);
            if (idx == 0)
            {
                out_indirectCommands[meshIdx].pipelineIndex = pipelineIdx;
                out_indirectCommands[meshIdx].indexCount    = meshInfo.indexCount;
                out_indirectCommands[meshIdx].firstIndex    = meshInfo.firstIndex;
                out_indirectCommands[meshIdx].vertexOffset  = meshInfo.vertexOffset;
                out_indirectCommands[meshIdx].firstInstance = firstInstance;
            }
        }

        out_meshInstanceIndexes[firstInstance + idx]      = instanceIdx;
        out_meshInstances[instanceIdx].posScale           = instancePosScale;
        out_meshInstances[instanceIdx].quat               = quat;
        out_meshInstances[instanceIdx].meshIdxMaterialIdx = instance.meshIdxMaterialIdx;
    }
}