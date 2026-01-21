#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

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
    uint _padding;
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
struct OutIndirectCommand
{
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
#define MAX_RENDERNODE_ENTRIES 7
struct HashTableEntry
{
    uint16_t numObjects;
    uint16_t renderNodeIndexes[MAX_RENDERNODE_ENTRIES];
};
layout (binding = 5, std430) buffer OutObjectBounds
{
    vec4 out_objectBounds[];
};
layout (binding = 6, std430) buffer InGridTable
{
    ivec3 in_gridSize;
    int in_tableSize;
    HashTableEntry in_gridTable[];
};


void main()
{
    const uint instanceIdx   = gl_GlobalInvocationID.x;
    const uint16_t meshIdx   = uint16_t(in_instances[instanceIdx].meshIdxMaterialIdx & 0x0000FFFF);
    const uint renderNodeIdx = in_instances[instanceIdx].renderNodeIdx;
    const uint offsetIdx     = in_instances[instanceIdx].instanceOffsetIdx;

    const vec4 quat                   = quat_multiply(in_renderNodeTransforms[renderNodeIdx].quat, in_instanceOffsets[offsetIdx].quat);
    const vec4 renderNodePosScale     = in_renderNodeTransforms[renderNodeIdx].posScale;
    const vec4 instanceOffsetPosScale = in_instanceOffsets[offsetIdx].posScale;
    const vec4 instancePosScale       = vec4(renderNodePosScale.xyz + quat_transform(instanceOffsetPosScale.xyz, in_renderNodeTransforms[renderNodeIdx].quat),
                                            renderNodePosScale.w * instanceOffsetPosScale.w);
    const vec3 centerPos              = quat_transform(in_meshInfos[meshIdx].center * instancePosScale.w, quat);
    const float radius                = in_meshInfos[meshIdx].radius * instancePosScale.w;

    const vec3 minPos = centerPos - vec3(radius);
    const vec3 maxPos = centerPos + vec3(radius);

    const ivec3 minGrid = ivec3(floor(minPos / GRID_SIZE));
    const ivec3 maxGrid = ivec3(floor(maxPos / GRID_SIZE));
    for (int x = minGrid.x; x <= maxGrid.x; ++x)
    {
        //...

    }
}