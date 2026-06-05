#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_debug_printf : enable

#include "shared.inc.glsl"

struct MaterialInfo
{
	uint flags;
    float opacity;
    uint diffuseNormalTexIdx;
	uint metalRoughnessTexIdxAlphaMode;
};
struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float width;     // 0 = point light, > 0 = rectangular area light
	vec3 direction;  // area light: up-axis, magnitude = height
	float rotation;  // area light: rotation of the quad around direction
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
float DistributionGGX(const float NdotH, const float a2)
{
	float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
	return a2 / (PI * denom * denom);
}
// Height-correlated Smith (Heitz 2014). Returns the *visibility* term G2 / (4 * NdotV * NdotL),
// i.e. the geometry term with the BRDF denominator already folded in. a2 = alpha^2 = roughness^2.
float V_SmithGGXCorrelated(const float NdotV, const float NdotL, const float a2)
{
	float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
	float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
	return 0.5 / max(ggxV + ggxL, 1e-5);
}
// Cheaper approximation of the above (Filament): trades the two sqrt for a linear mix. a = alpha = roughness.
float V_SmithGGXCorrelatedFast(const float NdotV, const float NdotL, const float a)
{
	float ggxV = NdotL * (NdotV * (1.0 - a) + a);
	float ggxL = NdotV * (NdotL * (1.0 - a) + a);
	return 0.5 / max(ggxV + ggxL, 1e-5);
}
// Roughness-aware variant: ceiling rises toward (1 - roughness) at grazing angles. Intended for IBL/ambient.
vec3 FresnelSchlickRoughness(float HdotV, vec3 F0, float roughness)
{
	float x = 1.0 - HdotV;
	float x2 = x * x;
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * (x2 * x2 * x);
}
// Plain Schlick: correct for direct/punctual lights (ceiling is white at grazing angles).
vec3 FresnelSchlick(float HdotV, vec3 F0)
{
	float x = clamp(1.0 - HdotV, 0.0, 1.0);
	float x2 = x * x;
	return F0 + (1.0 - F0) * (x2 * x2 * x);
}

vec3 doLightDiffuseOnly(vec3 lightRadiance, vec3 F, vec3 matColOverPi, float metalness, float NdotL)
{
	vec3 kD = (1.0 - F) * (1.0 - metalness);
	return kD * matColOverPi * lightRadiance * NdotL;
}

vec3 doPointLightSpecular(vec3 lightRadiance, vec3 F, float NdotL, float NdotV, float NdotH,
	float roughness, float a2)
{
	float NDF = DistributionGGX(NdotH, a2);
	float Vis = V_SmithGGXCorrelatedFast(NdotV, NdotL, roughness);
	return (NDF * Vis * F) * lightRadiance * NdotL;
}

vec3 doAreaLightSpecular(vec3 lightRadiance, vec3 Lspec, vec3 V, vec3 N, vec3 specularCol,
	float lightSize, float distSpec, float roughness, float roughnessSq)
{
	float alphaPrime = clamp(roughness + lightSize / (2.0 * distSpec), 0.0, 1.0);
	float ap2        = alphaPrime * alphaPrime;
	float sphereNorm = roughnessSq / max(ap2, 1e-5);

	vec3  H      = normalize(Lspec + V);
	float NdotL  = max(dot(N, Lspec), 0.0);
	float NdotV  = max(dot(N, V), 0.0);
	float HdotV  = max(dot(H, V), 0.0);
	float NdotH  = max(dot(N, H), 0.0);

	vec3  F   = FresnelSchlick(HdotV, specularCol);
	float NDF = DistributionGGX(NdotH, ap2) * sphereNorm;
	float Vis = V_SmithGGXCorrelatedFast(NdotV, NdotL, roughness);
	return (NDF * Vis * F) * lightRadiance * NdotL;
}

vec3 doLight(vec3 lightRadiance, vec3 L, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi,
	float metalness, float roughness, float roughnessSq)
{
	vec3 H = normalize(L + V);
	float NdotL = max(dot(N, L), 0.0);
	float NdotV = max(dot(N, V), 0.0);
	float HdotV = max(dot(H, V), 0.0);
	float NdotH = max(dot(N, H), 0.0);

	vec3 F = FresnelSchlick(HdotV, specularCol);
	vec3 specular = doPointLightSpecular(lightRadiance, F, NdotL, NdotV, NdotH, roughness, roughnessSq);
	return specular + doLightDiffuseOnly(lightRadiance, F, matColOverPi, metalness, NdotL);
}

vec3 doPointLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, 
	float metalness, float roughness, float roughnessSq)
{
	vec3 lightVec = light.pos - pos;
	float dist = length(lightVec);
	vec3 L = lightVec / dist;
	float falloff = squareFalloff(dist, light.range);
	vec3 lightRadiance = light.color * falloff;
	return doLight(lightRadiance, L, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
}

vec3 doSpotLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi,
	float metalness, float roughness, float roughnessSq)
{
	vec3 lightVec = light.pos - pos;
	float dist = length(lightVec);
	vec3 L = lightVec / dist;
	float falloff = squareFalloff(dist, light.range);

	// rotation holds the cone half-angle; direction's length is the edge softness (small = sharp).
	float softness = length(light.direction);
	float cosAngle = dot(-L, light.direction / softness);
	float cosOuter = cos(light.rotation);
	float cosInner = mix(cosOuter, 1.0, softness);
	float spot = smoothstep(cosOuter, cosInner, cosAngle);
	vec3 lightRadiance = light.color * falloff * spot;
	return doLight(lightRadiance, L, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
}

vec3 doSunLight(vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi,
	float metalness, float roughness, float roughnessSq)
{
	vec3 L = normalize(vec3(0.5, 1.0, 0.5));
	vec3 lightRadiance = vec3(1.0, 1.0, 1.0) * 2.5f;
	return doLight(lightRadiance, L, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
}

vec3 closestPointOnSegment(vec3 p, vec3 a, vec3 b)
{
	vec3 ab = b - a;
	float t = clamp(dot(p - a, ab) / dot(ab, ab), 0.0, 1.0);
	return a + ab * t;
}

vec3 closestPointOnRect(vec3 p, vec3 center, vec3 right, vec3 up, float halfWidth, float halfHeight)
{
	vec3 d = p - center;
	float x = clamp(dot(d, right), -halfWidth, halfWidth);
	float y = clamp(dot(d, up), -halfHeight, halfHeight);
	return center + right * x + up * y;
}

vec3 doAreaLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi,
	float metalness, float roughness, float roughnessSq)
{
	float height = length(light.direction);
	if (height < 1e-5 || light.width < 1e-5)
		return vec3(0.0);

	vec3 up = light.direction / height;
	vec3 ref = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 right0 = normalize(cross(up, ref));
	float cs = cos(light.rotation);
	float sn = sin(light.rotation);
	vec3 right = right0 * cs + cross(up, right0) * sn;
	vec3 quadNormal = cross(up, right);
	float halfWidth = light.width * 0.5;
	float halfHeight = height * 0.5;
	float lightSize = (halfWidth + halfHeight) * 0.5;
	vec3 center = light.pos;

	// One-sided emitter: ignore fragments behind the quad.
	if (dot(pos - center, quadNormal) <= 0.0)
		return vec3(0.0);

	// ---- Diffuse -------------------------------------------------------------
	vec3 closest = closestPointOnRect(pos, center, right, up, halfWidth, halfHeight);
	vec3 LdiffVec = closest - pos;
	float distDiff = length(LdiffVec);
	vec3 Ldiff = LdiffVec / max(distDiff, 1e-5);
	float facingDiff = max(dot(quadNormal, -Ldiff), 0.0);

	// Horizon clamp: smoothstep the center N.L over the quad's angular half-extent.
	// As the quad clips below the horizon it fades out proportionally to its apparent size.
	float NdotLcenter = dot(N, normalize(center - pos));
	float halfExtent = lightSize / max(distDiff, 1e-5);
	float horizonVis = smoothstep(-halfExtent, halfExtent, NdotLcenter);

	vec3 diffRadiance = light.color * squareFalloff(distDiff, light.range) * facingDiff;
	vec3 Hdiff = normalize(Ldiff + V);
	vec3 Fdiff = FresnelSchlick(max(dot(Hdiff, V), 0.0), specularCol);
	vec3 diffuse = doLightDiffuseOnly(diffRadiance, Fdiff, matColOverPi, metalness, horizonVis);

	// ---- Specular (representative point) -------------------------------------
	// Intersect the mirror ray with the quad plane and clamp to the rectangle, then shade from
	// that point with its own distance and facing (skipped for rough surfaces: lobe is broad).

	vec3 Fspec;
	vec3 Lspec;
	vec3 specRadiance;
	if (roughness < 0.6)
	{
		vec3 specPoint = closest;
		vec3 R = reflect(-V, N);
		float d = dot(R, quadNormal);
		if (d < 0.0)
		{
			float t = dot(center - pos, quadNormal) / d;
			if (t > 0.0)
				specPoint = closestPointOnRect(pos + R * t, center, right, up, halfWidth, halfHeight);
		}

		vec3 LspecVec = specPoint - pos;
		float distSpec = max(length(LspecVec), 1e-4);
		Lspec = LspecVec / distSpec;

		float facingSpec = max(dot(quadNormal, -Lspec), 0.0);
		specRadiance = light.color * squareFalloff(distSpec, light.range) * facingSpec;

		vec3 Hspec = normalize(Lspec + V);
		float HdotVspec = max(dot(Hspec, V), 0.0);
		Fspec = FresnelSchlick(HdotVspec, specularCol);
	}
	else
	{
		Lspec = Ldiff;
		specRadiance = diffRadiance;
		Fspec = Fdiff;
	}

	vec3 Hspec = normalize(Lspec + V);
	float NdotVspec = max(dot(N, V), 0.0);
	float NdotLspec = max(dot(N, Lspec), 0.0);
	float NdotHspec = max(dot(N, Hspec), 0.0);
	vec3 specular = doPointLightSpecular(specRadiance, Fspec, NdotLspec, NdotVspec, NdotHspec, roughness, roughnessSq);

	return diffuse + specular;
}

vec3 doTubeLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi,
	float metalness, float roughness, float roughnessSq)
{
	float height  = length(light.direction);
	float halfLen = height * 0.5;
	float radius  = abs(light.width);
	float absRange = abs(light.range);
	if (halfLen < 1e-5 || absRange < 1e-5)
		return vec3(0.0);

	vec3 axis = light.direction / height;
	vec3 pa   = light.pos - axis * halfLen;
	vec3 pb   = light.pos + axis * halfLen;

	// ---- Diffuse -------------------------------------------------------------
	// Closest point on the core segment drives distance falloff and the horizon fade.
	vec3 closest   = closestPointOnSegment(pos, pa, pb);
	vec3 LdiffVec  = closest - pos;
	float coreDist = length(LdiffVec);
	vec3 Ldiff     = LdiffVec / max(coreDist, 1e-5);
	float distDiff = max(coreDist - radius, 1e-4); // shade from the tube surface, not its core

	// Horizon clamp: fade as the tube sinks below the tangent plane, scaled by its apparent size.
	float NdotLcenter = dot(N, normalize(light.pos - pos));
	float halfExtent  = (halfLen + radius) / max(coreDist, 1e-5);
	float horizonVis  = smoothstep(-halfExtent, halfExtent, NdotLcenter);

	vec3 diffRadiance = light.color * squareFalloff(distDiff, absRange);
	vec3 Hdiff = normalize(Ldiff + V);
	vec3 Fdiff = FresnelSchlick(max(dot(Hdiff, V), 0.0), specularCol);
	vec3 diffuse = doLightDiffuseOnly(diffRadiance, Fdiff, matColOverPi, metalness, horizonVis);

	// ---- Specular (representative point) -------------------------------------
	// Find the point on the tube surface most aligned with the mirror ray R, then shade it as a
	// punctual light *from that point* (its own distance/direction) with a size-widened lobe.
	vec3  Lspec;
	float distSpec;
	if (roughness < 0.6)
	{
		vec3 l0      = pa - pos;
		vec3 l1      = pb - pos;
		vec3 R       = reflect(-V, N);
		vec3 ld      = l1 - l0;
		float RdotLd = dot(R, ld);
		float ldLen2 = dot(ld, ld);
		float denom  = ldLen2 - RdotLd * RdotLd;
		float t = (abs(denom) > 1e-6) ? clamp((dot(R, l0) * RdotLd - dot(l0, ld)) / denom, 0.0, 1.0) : 0.5;
		vec3 closestLine = l0 + ld * t;                     // closest core point to the reflection ray
		vec3 centerToRay = dot(closestLine, R) * R - closestLine;
		float ctrLen     = length(centerToRay);
		vec3 specVec     = closestLine + centerToRay * clamp(radius / max(ctrLen, 1e-5), 0.0, 1.0);\
		float specLen    = length(specVec);
		distSpec = max(specLen - radius, 1e-4);           // falloff at the actual specular distance
		Lspec    = specVec / max(specLen, 1e-5);
	}
	else
	{   // broad lobe: the representative point barely matters, reuse closest
		distSpec = coreDist;
		Lspec    = Ldiff;
	}
	vec3 specRadiance = light.color * squareFalloff(distSpec, absRange);
	vec3 specular = doAreaLightSpecular(specRadiance, Lspec, V, N, specularCol, radius, distSpec, roughness, roughnessSq);

	return diffuse + specular;
}

// Dispatches a unified light by its width discriminant: > 0 area, < 0 spot, == 0 point.
vec3 doLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi,
	float metalness, float roughness, float roughnessSq)
{
	if (light.width > 0.0)
	{
		if (light.range < 0.0)
			return doTubeLight(light, pos, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
		return doAreaLight(light, pos, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
	}
	if (light.width < 0.0)
		return doSpotLight(light, pos, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
	return doPointLight(light, pos, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
}

void main()
{
	const uint16_t materialIdx = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
	const MaterialInfo material = in_materialInfos[materialIdx];
	const uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);
	const uint16_t normalTexIdx  = uint16_t((material.diffuseNormalTexIdx & 0xFFFF0000) >> 16);
	const uint16_t metalRoughnessTexIdx = uint16_t(material.metalRoughnessTexIdxAlphaMode & 0x0000FFFF);
	const uint16_t alphaMode = uint16_t((material.metalRoughnessTexIdxAlphaMode & 0xFFFF0000) >> 16);

	float roughness = 1.0;
	float metalness = 0.0;
	if (metalRoughnessTexIdx != uint16_t(0xFFFF))
	{
		const vec2 metalRoughness = texture(u_textures[metalRoughnessTexIdx], in_uv).bg;
		metalness = metalRoughness.x;
		roughness = max(metalRoughness.y, 0.01);
	}
	//roughness = 0.70;

	const vec3 V  = normalize(u_viewPos - in_pos);
	const vec2 uv = in_uv; //spomDisplaceUV(uint(normalTexIdx), V);

	const vec4 diffuseSample = texture(u_textures[diffuseTexIdx], uv);
	if (alphaMode == ALPHA_MODE_MASK && diffuseSample.a < material.opacity)
		discard;
	const vec3 materialColor = diffuseSample.xyz;
	const vec3 materialNormal = texture(u_textures[normalTexIdx], uv).xyz;
	const vec3 specularColor = mix(vec3(0.04), materialColor, metalness);

	const vec3 N = in_tbn * normalize(materialNormal * 2.0 - 1.0);

	const float roughnessSq = roughness * roughness;
	const vec3 matColOverPi = materialColor / PI;
	
	const float ambient = 0.05f;
	vec3 color = materialColor * ambient;

	//color += doSunLight(V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);

	const ivec3 gridPos = getGridPos(in_pos);
    uint tableIdx = getPositionHash(gridPos) & (in_tableSize - 1); // tableSize is a power of two
	
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
				color += doLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
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
				color += doLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
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
// if (distance(in_pos, light.pos) < abs(light.range))
// {
// 	color += vec3(0.0, 0.0, 0.05);
// }