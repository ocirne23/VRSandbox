#version 450

// GI probe debug visualization fragment: simple directional shade for depth perception.

layout (location = 0) in vec3 v_color;
layout (location = 1) in vec3 v_normal;

layout (location = 0) out vec4 out_color;

void main()
{
    float ndl = max(dot(normalize(v_normal), normalize(vec3(0.4, 0.8, 0.5))), 0.0) * 0.7 + 0.3;
    out_color = vec4(v_color * ndl, 1.0);
}
