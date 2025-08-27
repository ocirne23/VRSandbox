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

vec3 quat_transform(vec3 vec, vec4 quat)
{
    vec3 r2, r3;
    r2 = cross(quat.xyz, vec);
    r2 = quat.w * vec + r2;
    r3 = cross(quat.xyz, r2);
    r3 = r3 * 2 + vec;
    return r3;
}
//vec3 quat_transform( vec3 v, vec4 q)
//{
//    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
//}
//vec3 quat_transform(vec3 v, vec4 q) {
//    vec3 t = 2.0 * cross(q.xyz, v);
//    return v + q.w * t + cross(q.xyz, t);
//}

vec4 quat_multiply(vec4 q, vec4 p)
{
    vec4 c, r;
    c.xyz = cross(q.xyz, p.xyz);
    c.w = -dot(q.xyz, p.xyz);
    r = p * q.w + c;
    r.xyz = (q * p.w + r).xyz;
    return r;
}
//vec4 quat_multiply(vec4 a, vec4 b) {
//    return vec4(
//        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
//        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
//        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
//        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z
//    );
//}
//vec4 quat_multiply(vec4 a, vec4 b) {
//    return vec4(a.w * b.xyz + b.w * a.xyz + cross(a.xyz, b.xyz), a.w * b.w - dot(a.xyz, b.xyz));
//}
//vec4 quat_invert(vec4 q) {
//    return vec4(-q.xyz, q.w);
//}

float3x4 convert_dq_to_mat(float4 nq, float4 dq)
{
    float3  t = (nq.w * dq.xyz - dq.w * nq.xyz + cross(nq.xyz, dq.xyz));
    float3 v2 = nq.xyz + nq.xyz;
    float xx = 1 - v2.x * nq.x; float yy = v2.y * nq.y; float xw = v2.x * nq.w;
    float xy = v2.y * nq.x;   float yz = v2.z * nq.y; float yw = v2.y * nq.w;
    float xz = v2.z * nq.x;   float zz = v2.z * nq.z; float zw = v2.z * nq.w;
    float3x4 r;
    r._m00 = 1 - yy - zz; r._m01 = xy - zw; r._m02 = xz + yw; r._m03 = t.x + t.x;
    r._m10 = xy + zw;   r._m11 = xx - zz; r._m12 = yz - xw; r._m13 = t.y + t.y;
    r._m20 = xz - yw;   r._m21 = yz + xw; r._m22 = xx - yy; r._m23 = t.z + t.z;
    return r;
}

//float4 mat_to_quat(float3x3 m)
//{
//	float4 q;
//	float s,p,tr = m._m00 + m._m11 + m._m22;
//	q.w=1;q.x=0;q.y=0;q.z=0;
//	if(tr>0)
//	{
//		s=sqrt(tr+1.0f);
//		p=0.5f/s;
//		q.w=s*0.5f;
//		q.x=(m._m21-m._m12)*p;
//		q.y=(m._m02-m._m20)*p;
//		q.z=(m._m10-m._m01)*p;
//	}
//	else if ((m._m00>=m._m11) && (m._m00>=m._m22))
//	{
//		s=sqrt(m._m00-m._m11-m._m22+1.0f);
//		p=0.5f/s;
//		q.w=(m._m21-m._m12)*p;
//		q.x=s*0.5f;
//		q.y=(m._m10+m._m01)*p;
//		q.z=(m._m20+m._m02)*p;
//	}
//	else if ((m._m11>=m._m00) && (m._m11>=m._m22))
//	{
//		s=sqrt(m._m11-m._m22-m._m00+1.0f);
//		p=0.5f/s;
//		q.w=(m._m02-m._m20)*p;
//		q.x=(m._m01+m._m10)*p;
//		q.y=s*0.5f;
//		q.z=(m._m21+m._m12)*p;
//	}
//	else if ((m._m22>=m._m00) && (m._m22>=m._m11))
//	{
//		s=sqrt(m._m22-m._m00-m._m11+1.0f);
//		p=0.5f/s;
//		q.w=(m._m10-m._m01)*p;
//		q.x=(m._m02+m._m20)*p;
//		q.y=(m._m12+m._m21)*p;
//		q.z=s*0.5f;
//	}
//	return q;
//}



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
    const uint instanceIdx   = gl_GlobalInvocationID.x;
    const uint16_t meshIdx   = uint16_t(in_instances[instanceIdx].meshIdxMaterialIdx & 0x0000FFFF);
    const uint renderNodeIdx = in_instances[instanceIdx].renderNodeIdx;
    const uint offsetIdx     = in_instances[instanceIdx].instanceOffsetIdx;

    out_indirectCommands[meshIdx].indexCount    = in_meshInfos[meshIdx].indexCount;
    out_indirectCommands[meshIdx].firstIndex    = in_meshInfos[meshIdx].firstIndex;
    out_indirectCommands[meshIdx].vertexOffset  = in_meshInfos[meshIdx].vertexOffset;
    out_indirectCommands[meshIdx].firstInstance = in_firstInstances[meshIdx];

    const vec4 quat                   = quat_multiply(in_renderNodeTransforms[renderNodeIdx].quat, in_instanceOffsets[offsetIdx].quat);
    const vec4 renderNodePosScale     = in_renderNodeTransforms[renderNodeIdx].posScale;
    const vec4 instanceOffsetPosScale = in_instanceOffsets[offsetIdx].posScale;
    const vec4 instancePosScale       = vec4(renderNodePosScale.xyz + quat_transform(instanceOffsetPosScale.xyz, in_renderNodeTransforms[renderNodeIdx].quat),
                                            renderNodePosScale.w * instanceOffsetPosScale.w);
    const vec3 centerPos              = quat_transform(in_meshInfos[meshIdx].center, quat);
    const float radius                = in_meshInfos[meshIdx].radius * instancePosScale.w;

    if (frustumCheck(instancePosScale.xyz + centerPos, radius))
    {
        const uint idx = atomicAdd(out_indirectCommands[meshIdx].instanceCount, 1);
        out_meshInstanceIndexes[in_firstInstances[meshIdx] + idx] = instanceIdx;
        out_meshInstances[instanceIdx].posScale           = instancePosScale;
        out_meshInstances[instanceIdx].quat               = quat;
        out_meshInstances[instanceIdx].meshIdxMaterialIdx = in_instances[instanceIdx].meshIdxMaterialIdx;
    }
}