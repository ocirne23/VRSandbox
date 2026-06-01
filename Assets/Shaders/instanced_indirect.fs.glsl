#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable

#include "shared_constants.inc.glsl"

struct MaterialInfo
{
    vec3 baseColor;
    float roughness;
    vec3 specularColor;
    float metalness;
    vec3 emissiveColor;
    uint diffuseNormalTexIdx;
	float opacity;
    uint16_t alphaMode;
};
struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float intensity;
};
layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};
layout (binding = 2, std430) readonly buffer InMaterialInfos
{
	MaterialInfo in_materialInfos[];
};
layout (binding = 4, std430) readonly buffer InLightInfos
{
	LightInfo in_lightInfos[];
};
layout (binding = 5, std430) readonly buffer InLightGrid
{
    uint in_gridData[];
};
layout (binding = 6, std430) readonly buffer InGridTable
{
	uint in_numGrids;
	uint in_gridDataCounter;
    uint in_tableSize;
    uint in_gridTable[];
};

layout (binding = 7) uniform sampler2D u_textures[];

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec4 out_color;

const float PI = 3.14159265359;

vec3 randomColor(uint seed) 
{
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    vec3 bits = vec3(float(seed & 255u), float((seed >> 8) & 255u), float((seed >> 16) & 255u));
    return bits / 255.0;
}
vec3 randomColor(ivec3 seed) 
{
    uvec3 u = uvec3(seed);
    uint hash = u.x * 1597334673u + u.y * 3812015801u + u.z * 2798796415u;
	return randomColor(hash);
}
uint getHashTableIdx(ivec3 p, uint tableSize) {
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return uint(n % tableSize);
}
ivec3 getGridMin(uint gridIdx) 
{ 
    return ivec3(in_gridData[gridIdx + 0], in_gridData[gridIdx + 1], in_gridData[gridIdx + 2]); 
}
uint getCellSize(uint gridIdx) 
{ 
    return in_gridData[gridIdx + 3]; 
}
uint getCellOffset(uint gridIdx, uint cellIdx)
{
    return gridIdx + 8 + cellIdx * (MAX_LIGHTCELL_LIGHTS / 2 + 1);
}
ivec3 getGridPos(vec3 pos)
{
    return ivec3(floor(pos / GRID_SIZE));
}
uint getLightId(uint cellOffset, uint lightIdx) // return as uint instead of uint16 to avoid driver bug (treating uint16_t as signed)
{
    const uint packed = in_gridData[cellOffset + 1 + lightIdx / 2];
    if ((lightIdx & 1u) == 0u)
        return uint(packed & 0x0000FFFF);
    else
        return uint(packed >> 16);
}
uint getLargeLightCount(uint gridIdx)
{
	return in_gridData[gridIdx + 4];
}
uint getLargeLightId(uint gridIdx, uint lightIdx)
{
	const uint packed = in_gridData[gridIdx + 5 + lightIdx / 2];
	if ((lightIdx & 1u) == 0u)
		return uint(packed & 0x0000FFFF);
	else
		return uint(packed >> 16);
}
float squareFalloff(float dist, float lightRadius) 
{
    float attenuation = 1.0 / (dist * dist + 1.0);
    float dr = dist / lightRadius;
    float dr2 = dr * dr;
    float falloff = clamp(1.0 - dr2 * dr2, 0.0, 1.0);
    falloff *= falloff;
    return attenuation * falloff;
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
vec3 FresnelSchlickRoughness(float HdotV, vec3 F0, float roughness)
{
	float x = 1.0 - HdotV;
	float x2 = x * x;
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * (x2 * x2 * x);
}

vec3 doLight(vec3 pos, vec3 lightRadiance, vec3 L, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, 
	float metalness, float roughness, float roughness1, float roughness1sqOver8, float roughnessSq)
{
	vec3 H = normalize(L + V);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	float NDF = DistributionGGX(N, H, roughnessSq);
	float G = GeometrySmith(NdotL, NdotV, roughness1sqOver8);
	vec3 F = FresnelSchlickRoughness(HdotV, specularCol, roughness);
	vec3 specularContrib = NDF * G * F / max(4.0 * NdotV * NdotL, 0.0001);
	vec3 kD = (1.0 - F) * (1.0 - metalness);
	return (kD * matColOverPi + specularContrib) * lightRadiance * NdotL;
}

vec3 doPointLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, 
	float metalness, float roughness, float roughness1, float roughness1sqOver8, float roughnessSq)
{
	vec3 lightVec = light.pos - pos;
	float dist = length(lightVec);
	vec3 L = lightVec / dist;
	float falloff = squareFalloff(dist, light.range);
	vec3 lightRadiance = light.color * falloff * light.intensity;
	return doLight(pos, lightRadiance, L, V, N, specularCol, matColOverPi, 
		metalness, roughness, roughness1, roughness1sqOver8, roughnessSq);
}

vec3 doSunLight(vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, 
	float metalness, float roughness, float roughness1, float roughness1sqOver8, float roughnessSq)
{
	vec3 L = normalize(vec3(0.5, 1.0, 0.5));
	vec3 lightRadiance = vec3(1.0, 1.0, 1.0) * 2.5f;
	return doLight(pos, lightRadiance, L, V, N, specularCol, matColOverPi, 
		metalness, roughness, roughness1, roughness1sqOver8, roughnessSq);
}

void main()
{
	const uint16_t materialIdx = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
	const MaterialInfo material = in_materialInfos[materialIdx];
	const uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);
	const uint16_t normalTexIdx  = uint16_t((material.diffuseNormalTexIdx & 0xFFFF0000) >> 16);
	
	const float roughness = 0.4;//max(material.roughness, 0.01);
	const float metalness = 0.0;//material.metalness;
	
	const vec4 diffuseSample = texture(u_textures[diffuseTexIdx], in_uv);
	if (material.alphaMode == ALPHA_MODE_MASK && diffuseSample.a < material.opacity)
		discard;
	const vec3 materialColor = diffuseSample.xyz;
	const vec3 materialNormal = texture(u_textures[normalTexIdx], in_uv).xyz;
	const vec3 specularColor = mix(vec3(0.04), materialColor, material.metalness);
	
	const vec3 V = normalize(u_viewPos - in_pos);
	const vec3 N = in_tbn * normalize(materialNormal * 2.0 - 1.0);

	const float roughness1 = roughness + 1.0;
	const float roughness1sqOver8 = (roughness1 * roughness1) / 8.0;
	const float roughnessSq = roughness * roughness;
	const vec3 matColOverPi = materialColor / PI;
	
	const float ambient = 0.05f;
	vec3 color = materialColor * ambient;

	color += doSunLight(in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughness1, roughness1sqOver8, roughnessSq);

	const ivec3 gridPos = getGridPos(in_pos);
    uint tableIdx = getHashTableIdx(gridPos, in_tableSize);
	
	while (true)
	{
		const uint gridIdx = in_gridTable[tableIdx];
		if (gridIdx == EMPTY_ENTRY)
			break;
		const ivec3 gridMin = getGridMin(gridIdx);
		if (gridMin == gridPos)
		{
			const uint numLargeLights = getLargeLightCount(gridIdx);
			for (uint i = 0; i < min(numLargeLights, MAX_LARGE_LIGHTS_PER_GRID); ++i)
			{
				const uint lightId = getLargeLightId(gridIdx, i);
				const LightInfo light = in_lightInfos[lightId];
				color += doPointLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughness1, roughness1sqOver8, roughnessSq);
			}
			
			const uint cellSize = getCellSize(gridIdx);
			const uint numCells = GRID_SIZE / cellSize;
			const ivec3 cellPos = (ivec3(floor(in_pos)) - gridMin * GRID_SIZE) / int(cellSize);
			const uint numCellsSq = numCells * numCells;
			const uint cellIdx = cellPos.x + cellPos.y * numCells + cellPos.z * numCellsSq;
			const uint cellOffset = getCellOffset(gridIdx, cellIdx);
			const uint numLights = in_gridData[cellOffset];
			for (uint i = 0; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
			{
				const uint lightId = getLightId(cellOffset, i);
				const LightInfo light = in_lightInfos[lightId];
				color += doPointLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, 
									roughness, roughness1, roughness1sqOver8, roughnessSq);
			}
			break;
		}
		tableIdx = (tableIdx + 1) & (in_tableSize - 1);
	}
	
	out_color = vec4(color, min(diffuseSample.a, material.opacity));
}

// Visualize grids
// color += randomColor(gridMin) * 0.2;

// Visualize light
// color += vec3(0.0, 0.0, 0.05);
// if (distance(in_pos, light.pos) < light.range)
// {
// 	color += vec3(0.0, 0.0, 0.05);
// }