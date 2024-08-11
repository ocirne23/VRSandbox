#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct InMeshInstance
{
    vec4 posScale;
    vec4 quat;
    uint meshIdx;
};

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};
layout (binding = 1, std430) buffer InMeshInstances
{
    InMeshInstance in_instances[];
};

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec4 in_tangent;
layout (location = 3) in vec2 in_uv;

layout (location = 4) in uint inst_idx;

layout (location = 0) out vec3 out_pos;
layout (location = 1) out mat3 out_tbn;
layout (location = 4) out vec2 out_uv;
layout (location = 5) out flat uint out_meshIdx;

vec3 quat_transform( vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
    vec3  inst_pos   = in_instances[inst_idx].posScale.xyz;
    float inst_scale = in_instances[inst_idx].posScale.w;
    vec4  inst_quat  = in_instances[inst_idx].quat;

    vec3 N = quat_transform(in_normal, inst_quat);
    vec3 T = quat_transform(in_tangent.xyz, inst_quat);
    vec3 B = cross(N, T) * in_tangent.w;

    out_pos = quat_transform(in_pos * inst_scale, inst_quat) + inst_pos;
    out_tbn = (mat3(T, B, N));
    out_uv  = in_uv;
    out_meshIdx = in_instances[inst_idx].meshIdx;

    gl_Position = u_mvp * vec4(out_pos, 1.0);
}

//T = normalize(T - dot(T, N) * N);
