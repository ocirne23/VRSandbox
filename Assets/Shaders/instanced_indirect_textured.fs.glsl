#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};

layout (binding = 2) uniform sampler2D u_color[];
layout (binding = 3) uniform sampler2D u_normal[];
layout (binding = 4) uniform sampler2D u_roughness_metallic_height[];

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdx;

layout (location = 0) out vec3 out_color;

const float PI = 3.14159265359;

float inverseSquareFalloff(float lightDistance, float lightRange)
{
	return pow(max(1.0 - pow((lightDistance / lightRange), 4), 0), 2) / (pow(lightDistance, 2) + 1);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
vec3 diffuseBurley(vec3 diffuse, float Roughness, float NoV, float NoL, float VoH)
{
	float FD90 = VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * exp2( (-5.55473 * NoV - 6.98316) * NoV );
	float FdL = 1 + (FD90 - 1) * exp2( (-5.55473 * NoL - 6.98316) * NoL );
	return diffuse / PI * FdV * FdL * NoL;
}

// [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
vec3 diffuseOrenNayar( vec3 DiffuseColor, float Roughness, float NoV, float NoL, float VoH )
{
	float VoL = 2 * VoH - 1;
	float m = Roughness * Roughness;
	float m2 = m * m;
	float C1 = 1 - 0.5 * m2 / (m2 + 0.33);
	float Cosri = VoL - NoV * NoL;
	float C2 = 0.45 * m2 / (m2 + 0.09) * Cosri * ( Cosri >= 0 ? min( 1, NoL / NoV ) : NoL );
	return DiffuseColor / PI * ( NoL * C1 + C2 );
}

float specularBRDF(float NdotH, float LdotH, float roughness)
{
	const float PIdot4 = dot(4, PI);
	float roughness4 = roughness * roughness * roughness * roughness;
	float A = NdotH * NdotH * (roughness4 - 1) + 1;
	float B = dot(A * A, LdotH * LdotH * (roughness + 0.5));
	return roughness4 / dot(PIdot4, B);
}

float environmentContrib(float roughness, float NdotV)
{
	float a = 1.0 - max(roughness, NdotV);
	return a * a * a;
}

void main()
{
	vec3 lightPos  = vec3(12, 3, -2);
	vec3 lightVec  = lightPos - in_pos;
	float distance = length(lightVec);
	float falloff  = inverseSquareFalloff(distance, 30);
	float intensity = 65.0;

	vec3 V = normalize(u_viewPos - in_pos);
	vec3 L = normalize(lightVec);
	vec3 H = normalize(L + V);
	vec3 N = normalize(texture(u_normal[in_meshIdx], in_uv).xyz * 2.0 - 1.0);
	N = normalize(in_tbn * N);

	float NdotH = clamp(dot(N, H), 0.0, 1.0);
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	float LdotH = dot(L, H);//clamp(dot(L, H), 0.0, 1.0);
	float NdotV = dot(N, V);//clamp(dot(N, V), 0.0, 1.0);
	float VdotH = dot(V, H);//clamp(dot(V, H), 0.0, 1.0);

	vec3 materialColor = texture(u_color[in_meshIdx], in_uv).xyz;
	float roughness    = texture(u_roughness_metallic_height[in_meshIdx], in_uv).x;

	float specular = NdotL > 0.0 ? specularBRDF(NdotH, LdotH, roughness) : 0.0;
	float env      = environmentContrib(roughness, NdotV);

	vec3 diffuse = diffuseBurley(materialColor, roughness, NdotV, NdotL, VdotH);
	vec3 color = (diffuse + (specular + env) * materialColor) * falloff * intensity;
	out_color = color;
}