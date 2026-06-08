#version 450

#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in vec3 in_normal;

layout (location = 0) out vec4 out_normal; // world-space normal, xyz (w unused)

void main()
{
    out_normal = vec4(normalize(in_normal), 0.0);
}
