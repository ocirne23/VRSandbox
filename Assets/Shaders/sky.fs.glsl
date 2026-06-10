#version 460

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Sky variant of the static-mesh pipeline (MeshShaderVariant::Sky): shades the inside of the sky
// sphere with the same analytic skyRadiance() the GI/AO miss paths use, so the visible sky always
// matches what rays that escape the scene return. Adds the sun disc (u_sunAngularCos) and an optional
// glow halo (u_sunGlow) on top — the only place the disc is rendered.

#include "shared.inc.glsl"

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec4 out_color;


void main()
{
	const vec3 dir = normalize(in_pos - u_viewPos);
	vec3 color = skyColor(dir);

	const vec3 L = normalize(u_sunDirection.xyz);
	const float cosAngle = dot(dir, L);
	if (u_sunAngularCos < 1.0) // 1.0 disables the disc
	{
		// Feather the rim over a small fraction of the disc's angular size so it isn't a hard aliased edge.
		const float feather = (1.0 - u_sunAngularCos) * 0.2;
		color += u_sunColor.rgb * smoothstep(u_sunAngularCos, u_sunAngularCos + feather, cosAngle);
	}
	if (u_sunGlow > 0.0)
		color += u_sunColor.rgb * pow(clamp(cosAngle, 0.0, 1.0), u_sunGlow * u_sunGlow) * u_skyIntensity;

	out_color = vec4(color, 1.0);
}
