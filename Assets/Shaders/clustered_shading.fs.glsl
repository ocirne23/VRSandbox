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
#define MAX_ENTRY_INDEXES 7
struct HashTableEntry
{
    uint numEntries;
    uint entries[MAX_ENTRY_INDEXES];
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

uint16_t getHashTableIdx(ivec3 p) {
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return uint16_t(n % in_tableSize);
}

ivec3 getGridPos(vec3 pos)
{
    return ivec3(floor(pos / GRID_SIZE));
}

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

    out_objectBounds[instanceIdx] = vec4(centerPos, radius);

    const vec3 minPos = centerPos - vec3(radius);
    const vec3 maxPos = centerPos + vec3(radius);

    const ivec3 minGrid = ivec3(floor(minPos / GRID_SIZE));
    const ivec3 maxGrid = ivec3(floor(maxPos / GRID_SIZE));
    for (int x = minGrid.x; x <= maxGrid.x; ++x)
    {
        for (int y = minGrid.y; y <= maxGrid.y; ++y)
        {
            for (int z = minGrid.z; z <= maxGrid.z; ++z)
            {
                const uint16_t gridIdx = getHashTableIdx(ivec3(x, y, z));
                uint16 entryIdx = atomicAdd(n_gridTable[gridIdx].numEntries);
                if (entryIdx < MAX_ENTRY_INDEXES)
                {
                    n_gridTable[gridIdx].entries[entryIdx] = instanceIdx;
                }
            }
        }
    }
}