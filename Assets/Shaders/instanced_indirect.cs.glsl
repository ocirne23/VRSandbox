#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

struct InMeshInstance
{
    vec4 posScale;
    vec4 quat;
    uint meshIdxMaterialIdx;
};

struct InMeshInfo
{
    vec3 center;
    float radius;
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};
struct OutInstancedIndirectCommand
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
layout (binding = 1, std430) readonly buffer InMeshInstances
{
    InMeshInstance in_instances[];
};
layout (binding = 2, std430) readonly buffer InMeshInfoBuffer
{
    InMeshInfo in_meshInfo[];
};
layout (binding = 3, std430) writeonly buffer OutMeshInstanceIndexes
{
    uint out_meshInstanceIndexes[];
};
layout (binding = 4, std430) writeonly buffer OutIndirectCommandBufer
{
    OutInstancedIndirectCommand out_indirectCommands[];
};

vec3 quat_transform( vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

bool frustumCheck(vec4 pos, float radius)
{
    // Check sphere against frustum planes
    for (int i = 0; i < 6; i++) 
    {
        if (dot(pos, u_frustumPlanes[i]) + radius < 0.0)
        {
            return false;
        }
    }
    return true;
}

void main()
{
    const uint instanceIdx   = gl_GlobalInvocationID.x;
    const uint16_t meshIdx   = uint16_t(in_instances[instanceIdx].meshIdxMaterialIdx & 0x0000FFFF);
    const uint firstInstance = in_meshInfo[meshIdx].firstInstance;

    out_indirectCommands[meshIdx].indexCount    = in_meshInfo[meshIdx].indexCount;
    out_indirectCommands[meshIdx].firstIndex    = in_meshInfo[meshIdx].firstIndex;
    out_indirectCommands[meshIdx].vertexOffset  = in_meshInfo[meshIdx].vertexOffset;
    out_indirectCommands[meshIdx].firstInstance = in_meshInfo[meshIdx].firstInstance;

    const vec3 centerPos   = quat_transform(in_meshInfo[meshIdx].center, in_instances[instanceIdx].quat);
    const vec4 instancePos = vec4(in_instances[instanceIdx].posScale.xyz + centerPos, 1.0);
    const float radius     = in_meshInfo[meshIdx].radius * in_instances[instanceIdx].posScale.w;
    if (frustumCheck(instancePos, radius))
    {
        const uint idx = atomicAdd(out_indirectCommands[meshIdx].instanceCount, 1);
        out_meshInstanceIndexes[firstInstance + idx] = instanceIdx;
    }
}