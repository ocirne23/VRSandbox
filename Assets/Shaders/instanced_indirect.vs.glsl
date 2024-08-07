#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (std140, binding = 0) uniform buffer
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};

layout (location = 0) in vec3 in_pos;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec4 in_tangent;
layout (location = 3) in vec2 in_uv;

// Instanced attributes
layout (location = 4) in vec3 inst_pos;
layout (location = 5) in float inst_scale;
layout (location = 6) in vec4 inst_quat;

layout (location = 0) out vec3 out_pos;
layout (location = 1) out mat3 out_tbn;
layout (location = 4) out vec2 out_uv;

vec3 quat_transform( vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

void main()
{
    vec3 pos = quat_transform(in_pos * inst_scale, inst_quat) + inst_pos;

    out_pos = pos;
    vec3 N = quat_transform(in_normal, inst_quat);
    vec3 T = quat_transform(in_tangent.xyz, inst_quat);
    //T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * in_tangent.w;
    out_tbn = (mat3(T, B, N));
    out_uv = in_uv;

    gl_Position = u_mvp * vec4(pos, 1.0);
}