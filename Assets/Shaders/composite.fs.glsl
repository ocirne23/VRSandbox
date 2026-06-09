#version 450

// Copies the TAA-resolved scene colour into the swapchain. The resolved image is full render-target sized;
// the area outside the editor viewport panel holds the scene clear colour and is later covered by ImGui.

layout (location = 0) in vec2 v_uv;
layout (binding = 0) uniform sampler2D u_resolved;
layout (location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(texture(u_resolved, v_uv).rgb, 1.0);
}
