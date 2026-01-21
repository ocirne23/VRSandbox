#version 450

#define GRID_SIZE 16
#define LIGHT_TABLE_NUM_ENTRIES 8
#define MAX_LIGHTCELL_LIGHTS 7

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable

struct MaterialInfo
{
    vec3 baseColor;
    float roughness;
    vec3 specularColor;
    float metalness;
    vec3 emissiveColor;
    uint diffuseNormalTexIdx;
};
struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float intensity;
};
struct LightCell
{
	uint16_t numLights;
	uint16_t lightIds[MAX_LIGHTCELL_LIGHTS];
};
struct LightGrid
{
	ivec3 gridMin;
	float _padding;
	LightCell lightCells[GRID_SIZE * GRID_SIZE * GRID_SIZE];
};
struct LightGridHashTableEntry
{
    uint16_t gridIndexes[LIGHT_TABLE_NUM_ENTRIES];
};

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};
layout (binding = 2, std430) buffer InMaterialInfos
{
	MaterialInfo in_materialInfos[];
};
layout (binding = 4, std430) buffer InLightInfos
{
	LightInfo in_lightInfos[];
};
layout (binding = 5, std430) buffer InLightGrid
{
    LightGrid in_lightGrids[];
};
layout (binding = 6, std430) buffer InGridTable
{
    ivec3 in_gridSize;
    int in_tableSize;
    LightGridHashTableEntry in_gridTable[];
};

layout (binding = 7) uniform sampler2D u_textures[];

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
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - HdotV, 5.0);
}

vec3 doLight(
	LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, 
	float metalness, float roughness, float roughness1, float roughness1sqOver8, float roughnessSq)
{
	vec3 lightVec = light.pos - pos;
	float falloff = inverseSquareFalloff(length(lightVec), light.range);
	vec3 L = normalize(lightVec);
	vec3 H = normalize(L + V);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 0.0);
	float HdotV = max(dot(H, V), 0.0);

	float NDF = DistributionGGX(N, H, roughnessSq);
	float G = GeometrySmith(NdotL, NdotV, roughness1sqOver8);
	vec3 F = FresnelSchlickRoughness(HdotV, specularCol, roughness);
	vec3 specularContrib = NDF * G * F / max(4.0 * NdotV * NdotL, 0.0001);
	vec3 radianceContrib = light.color * falloff * light.intensity;
	vec3 kD = (1.0 - F) * (1.0 - metalness);
	return (kD * matColOverPi + specularContrib) * radianceContrib * NdotL;
}

uint16_t getHashTableIdx(ivec3 p) {
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return uint16_t(n % in_tableSize);
}

ivec3 getGridPos(vec3 pos)
{
    return ivec3(floor(pos / GRID_SIZE));
}

void main()
{
	const uint16_t materialIdx = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
	const MaterialInfo material = in_materialInfos[materialIdx];
	const uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);
	const uint16_t normalTexIdx  = uint16_t((material.diffuseNormalTexIdx & 0xFFFF0000) >> 16);
	
	const float roughness = 0.4;//max(material.roughness, 0.01);
	const float metalness = 0.0;//material.metalness;
	
	const vec3 materialColor = texture(u_textures[diffuseTexIdx], in_uv).xyz;
	const vec3 materialNormal = texture(u_textures[normalTexIdx], in_uv).xyz;
	const vec3 specularColor = mix(vec3(0.04), materialColor, material.metalness);
	
	const vec3 V = normalize(u_viewPos - in_pos);
	const vec3 N = in_tbn * normalize(materialNormal * 2.0 - 1.0);

	const float roughness1 = roughness + 1.0;
	const float roughness1sqOver8 = (roughness1 * roughness1) / 8.0;
	const float roughnessSq = roughness * roughness;
	const vec3 matColOverPi = materialColor / PI;
	
	const float ambient = 0.10f;
	vec3 color = materialColor * ambient;

	const ivec3 gridPos = getGridPos(in_pos);
    const uint16_t tableIdx = getHashTableIdx(gridPos);
    for (int entry = 0; entry < LIGHT_TABLE_NUM_ENTRIES; ++entry)
    {
        const uint16_t gridIdx = in_gridTable[tableIdx].gridIndexes[entry];
        if (gridIdx != 0xFFFF && in_lightGrids[gridIdx].gridMin == gridPos)
        {
            //color += vec3(0.0, 0.0, 0.1);
			const ivec3 cellIdx = ivec3(floor(in_pos)) - in_lightGrids[gridIdx].gridMin * GRID_SIZE;
			if (cellIdx.x >= 0 && 
				cellIdx.y >= 0 && 
				cellIdx.z >= 0 &&
				cellIdx.x < GRID_SIZE && 
				cellIdx.y < GRID_SIZE && 
				cellIdx.z < GRID_SIZE)
			{
				const int flatCellIdx = cellIdx.x + cellIdx.y * GRID_SIZE + cellIdx.z * GRID_SIZE * GRID_SIZE;
                const uint16_t numLights = in_lightGrids[gridIdx].lightCells[flatCellIdx].numLights; 
				for (int i = 0; i < numLights; ++i)
				{
					const uint16_t lightIdx = in_lightGrids[gridIdx].lightCells[flatCellIdx].lightIds[i];
					const LightInfo light = in_lightInfos[lightIdx];
					color += doLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, 
					                 roughness, roughness1, roughness1sqOver8, roughnessSq);
					//color += vec3(0.0, 0.1, 0.0);
				}
			}
			break;
        }
    }
	out_color = color;
}