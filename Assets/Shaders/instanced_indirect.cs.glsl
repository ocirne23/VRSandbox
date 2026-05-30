#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

#define ALPHA_MODE_OPAQUE 0u
#define ALPHA_MODE_MASK 1u
#define ALPHA_MODE_BLEND 2u

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

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
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
    OutIndirectCommand out_indirectCommands[];
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
    const uint instanceIdx     = gl_GlobalInvocationID.x;
    const uint16_t meshIdx     = uint16_t(in_instances[instanceIdx].meshIdxMaterialIdx & 0x0000FFFF);
    const uint16_t materialIdx = uint16_t((in_instances[instanceIdx].meshIdxMaterialIdx & 0xFFFF0000) >> 16);
    const uint renderNodeIdx   = in_instances[instanceIdx].renderNodeIdx;
    const uint offsetIdx       = in_instances[instanceIdx].instanceOffsetIdx;
    const uint16_t pipelineIdx = uint16_t(in_instances[instanceIdx].pipelineIdxAlphaMode & 0x0000FFFF);
    const uint16_t alphaMode   = uint16_t((in_instances[instanceIdx].pipelineIdxAlphaMode & 0xFFFF0000) >> 16);
    const bool isTransparent   = alphaMode == ALPHA_MODE_BLEND;

    const vec4 quat                   = quat_multiply(in_renderNodeTransforms[renderNodeIdx].quat, in_instanceOffsets[offsetIdx].quat);
    const vec4 renderNodePosScale     = in_renderNodeTransforms[renderNodeIdx].posScale;
    const vec4 instanceOffsetPosScale = in_instanceOffsets[offsetIdx].posScale;
    const vec4 instancePosScale       = vec4(renderNodePosScale.xyz + quat_transform(instanceOffsetPosScale.xyz, in_renderNodeTransforms[renderNodeIdx].quat),
                                            renderNodePosScale.w * instanceOffsetPosScale.w);
    const vec3 centerOffset           = quat_transform(in_meshInfos[meshIdx].center * instancePosScale.w, quat);
    const float radius                = in_meshInfos[meshIdx].radius * instancePosScale.w;

    const vec3 centerPos = instancePosScale.xyz + centerOffset;

    // Route this mesh's draw to the opaque or transparent command buffer by alpha mode. All
    // instances of a mesh share its material (1:1 in the asset pipeline), so a mesh lands in exactly
    // one bucket and its per-mesh firstInstance region is used exclusively by that bucket.
    if (!isTransparent)
    {
        out_indirectCommands[meshIdx].pipelineIndex = pipelineIdx;
        out_indirectCommands[meshIdx].indexCount    = in_meshInfos[meshIdx].indexCount;
        out_indirectCommands[meshIdx].firstIndex    = in_meshInfos[meshIdx].firstIndex;
        out_indirectCommands[meshIdx].vertexOffset  = in_meshInfos[meshIdx].vertexOffset;
        out_indirectCommands[meshIdx].firstInstance = in_firstInstances[meshIdx];
    }
    else
    {
        out_transparentIndirectCommands[meshIdx].pipelineIndex = pipelineIdx;
        out_transparentIndirectCommands[meshIdx].indexCount    = in_meshInfos[meshIdx].indexCount;
        out_transparentIndirectCommands[meshIdx].firstIndex    = in_meshInfos[meshIdx].firstIndex;
        out_transparentIndirectCommands[meshIdx].vertexOffset  = in_meshInfos[meshIdx].vertexOffset;
        out_transparentIndirectCommands[meshIdx].firstInstance = in_firstInstances[meshIdx];
    }

    if (frustumCheck(centerPos, radius))
    {
        uint idx;
        if (!isTransparent)
            idx = atomicAdd(out_indirectCommands[meshIdx].instanceCount, 1);
        else
            idx = atomicAdd(out_transparentIndirectCommands[meshIdx].instanceCount, 1);
        out_meshInstanceIndexes[in_firstInstances[meshIdx] + idx] = instanceIdx;
        out_meshInstances[instanceIdx].posScale           = instancePosScale;
        out_meshInstances[instanceIdx].quat               = quat;
        out_meshInstances[instanceIdx].meshIdxMaterialIdx = in_instances[instanceIdx].meshIdxMaterialIdx;
    }
}