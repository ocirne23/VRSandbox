#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};

struct MaterialInfo
{
    vec3 baseColor;
    float roughness;
    vec3 specularColor;
    float metalness;
    vec3 emissiveColor;
    uint diffuseNormalTexIdx;
};
layout (binding = 2, std430) buffer InMaterialInfos
{
	MaterialInfo in_materialInfos[];
};

struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float intensity;
};
layout (binding = 4) buffer InLightInfos
{
	LightInfo in_lightInfos[];
};

struct LightCell
{
	uint16_t numLights;
	uint16_t lightIds[7];
};
layout (binding = 5) buffer InLightGrid
{
	ivec3 in_gridMin;
	float _paddingInLightGrid;
	ivec3 in_gridSize;
	float in_cellSize;
	LightCell in_lightGrid[];
};

layout (binding = 6) uniform sampler2D u_textures[];

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

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

// a2: pow(roughness, 2)
float DistributionGGX(const vec3 N, const vec3 H, const float a2)
{
	float NdotH = max(dot(N, H), 0.0);
	float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
	return a2 / (PI * denom * denom);
}

// k: (roughness + 1)^2 / 8.0
float GeometrySmith(const float NdotL, const float NdotV, const float k)
{
	float ggx1 = NdotV / (NdotV * (1.0 - k) + k);
	float ggx2 = NdotL / (NdotL * (1.0 - k) + k);
	return ggx1 * ggx2;
}

vec3 Fresnel(float HdotV, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
}

vec3 FresnelSchlickRoughness(float HdotV, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - HdotV, 5.0);
}

float environmentContrib(float roughness, float NdotV)
{
	float a = 1.0 - max(roughness, NdotV);
	return a * a * a;
}

vec3 applyFog(vec3 rgb, float distance, vec3 rayOri, vec3 rayDir)
{
	float c = 0.3;
	float b = 0.02;
	vec3 fogColor = vec3(0.5, 0.6, 0.7);
	float fogAmount = clamp(c * exp(-rayOri.y * b) * (1.0 - exp(-distance * rayDir.y * b)) / rayDir.y, 0.0, 1.0);
	return mix(rgb, fogColor, fogAmount);
}

void main()
{
	uint16_t materialIdx = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
	MaterialInfo material = in_materialInfos[materialIdx];
	uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);
	uint16_t normalTexIdx  = uint16_t((material.diffuseNormalTexIdx & 0xFFFF0000) >> 16);
	
	float roughness = 0.4;//max(material.roughness, 0.01);
	float metalness = 0.0;//material.metalness;
	
	vec3 materialColor = texture(u_textures[diffuseTexIdx], in_uv).xyz;
	vec3 materialNormal = texture(u_textures[normalTexIdx], in_uv).xyz;
	materialNormal = normalize(materialNormal * 2.0 - 1.0);
	vec3 specularColor = mix(vec3(0.04), materialColor, material.metalness);
	
	vec3 eyeVec = u_viewPos - in_pos;
	vec3 V = normalize(eyeVec);
	vec3 N = in_tbn * materialNormal;

	float r = roughness + 1.0;
	float k = (r * r) / 8.0;
	float a2 = roughness * roughness;

	const float ambient = 0.10f;
	vec3 color = materialColor * ambient;

	ivec3 cellIdx = ivec3(floor((in_pos - vec3(in_gridMin)) / in_cellSize));
	if (cellIdx.x >= 0 && 
		cellIdx.y >= 0 && 
		cellIdx.z >= 0 &&
		cellIdx.x < in_gridSize.x && 
		cellIdx.y < in_gridSize.y && 
		cellIdx.z < in_gridSize.z)
	{
		int flatCellIdx = cellIdx.x + cellIdx.y * in_gridSize.x + cellIdx.z * in_gridSize.x * in_gridSize.y;
		LightCell cell = in_lightGrid[flatCellIdx];
		for (int i = 0; i < cell.numLights; ++i)
		{
			int lightIdx = cell.lightIds[i];
			LightInfo light = in_lightInfos[lightIdx];
			vec3 lightVec = light.pos - in_pos;
			float distance = length(lightVec);
			float falloff  = inverseSquareFalloff(distance, light.range);
			float intensity = light.intensity;
			vec3 L = normalize(lightVec);
			vec3 H = normalize(L + V);
			float NdotL = clamp(dot(N, L), 0.0, 1.0);
			float NdotV = clamp(dot(N, V), 0.0, 1.0);
			float HdotV = clamp(dot(H, V), 0.0, 1.0);
			float NDF = DistributionGGX(N, H, a2);
			float G = GeometrySmith(NdotL, NdotV, k);
			vec3 F = FresnelSchlickRoughness(HdotV, specularColor, roughness);
			
			vec3 numerator = NDF * G * F;
			float denom = max(4.0 * NdotV * NdotL, 0.0001);
			vec3 specularContrib = numerator / denom;
			vec3 kD = (1.0 - F) * (1.0 - metalness);
			vec3 radianceContrib = light.color * falloff * intensity;
			color += (kD * (materialColor / PI) + specularContrib) * radianceContrib * NdotL;
		}
	}
	
	out_color = color;// applyFog(color, length(eyeVec), u_viewPos, V);
}