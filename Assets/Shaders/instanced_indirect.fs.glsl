#version 400

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (std140, binding = 0) uniform buffer
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};

layout (binding = 1) uniform sampler2D u_color;
layout (binding = 2) uniform sampler2D u_normal;
layout (binding = 3) uniform sampler2D u_roughness_metallic_height;

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;

layout (location = 0) out vec3 out_color;

const vec3 light_pos = vec3(12, 12, -3);
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
	float NdotH2 = NdotH * NdotH;
	float LdotH2 = LdotH * LdotH;

	float A = NdotH2 * (roughness4 - 1) + 1;
	float B = dot(A * A, LdotH2 * (roughness + 0.5));
	return roughness4 / dot(PIdot4, B);
}

float environmentContrib(float roughness, float NdotV)
{
	float a = 1.0 - max(roughness, NdotV);
	return a * a * a;
}

void main()
{
	vec3 V = normalize(u_viewPos - in_pos);
	vec3 lightVec = light_pos - in_pos;
	float distance = length(lightVec);
	vec3 L = normalize(lightVec);

	float falloff = inverseSquareFalloff(distance, 30);

	vec3 N = normalize(texture(u_normal, in_uv).xyz * 2.0 - 1.0);
	N = normalize(in_tbn * N);

	vec3 materialColor = texture(u_color, in_uv).xyz;
	vec3 roughnessMetallicHeight = texture(u_roughness_metallic_height, in_uv).xyz;
	float roughness = roughnessMetallicHeight.x;

	vec3 H = normalize(L + V);
	float NdotH = clamp(dot(N, H), 0.0, 1.0);
	float LdotH = dot(L, H);//clamp(dot(L, H), 0.0, 1.0);
	float NdotV = dot(N, V);//clamp(dot(N, V), 0.0, 1.0);
	float NdotL = dot(N, L);//clamp(dot(N, L), 0.0, 1.0);
	float VdotH = dot(V, H);//clamp(dot(V, H), 0.0, 1.0);

	float specular = specularBRDF(NdotH, LdotH, roughness);
	float env = environmentContrib(roughness, NdotV);

	vec3 diffuse = diffuseBurley(materialColor, roughness, NdotV, NdotL, VdotH);
	vec3 color = (diffuse + (specular + env) * materialColor) * falloff * 25.0f;
	out_color = color;
}