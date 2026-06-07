#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable
//#extension GL_EXT_debug_printf : enable

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
    vec4 u_sunDirection;  // xyz = normalized direction towards the sun
    vec4 u_sunColor;      // rgb = color * intensity
    mat4 u_cascadeViewProj[NUM_SHADOW_CASCADES]; // m[0][3] = far distance, m[1][3] = texel size (see cascade* below)
    vec4 u_shadowParams;  // x = depth bias, y = normal bias (texels), z = 1/resolution, w = pcf radius
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
layout (binding = 8) uniform sampler2DArrayShadow u_shadowMap;      // comparison sampler (hardware PCF)
layout (binding = 9) uniform sampler2DArray u_shadowMapDepth;       // raw depth (PCSS blocker search)

// GI irradiance probes (diffuse indirect). Fixed camera-anchored volume; written by the probe-trace pass.
layout (binding = 10, std430) readonly buffer ProbeShBuffer { float probe_sh[]; };
layout (binding = 11, std430) readonly buffer GiVolumeBuffer { ivec4 u_giVolumeMin; }; // xyz = volume min probe coord

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec4 out_color;

#include "shadows.inc.glsl"

#define GRID_DATA_NAME         in_gridData
#define GRID_TABLE_NAME 	   in_gridTable
#define TABLE_SIZE_NAME        in_tableSize
#include "light_grid.inc.glsl"

#define PROBE_SH_NAME          probe_sh
#include "gi_probe.inc.glsl"

float squareFalloff(float dist, float lightRadius) 
{
    float attenuation = 1.0 / (dist * dist + 1.0);
    float dr = dist / lightRadius;
    float dr2 = dr * dr;
    float falloff = clamp(1.0 - dr2 * dr2, 0.0, 1.0);
    falloff *= falloff;
    return attenuation * falloff;
}
float DistributionGGX(const float NdotH, const float roughnessSq)
{
	float denom = (NdotH * NdotH) * (roughnessSq - 1.0) + 1.0;
	return roughnessSq / (PI * denom * denom);
}
float V_SmithGGXCorrelated(const float NdotV, const float NdotL, const float roughnessSq)
{
	float ggxV = NdotL * sqrt(NdotV * NdotV * (1.0 - roughnessSq) + roughnessSq);
	float ggxL = NdotV * sqrt(NdotL * NdotL * (1.0 - roughnessSq) + roughnessSq);
	return 0.5 / max(ggxV + ggxL, 1e-5);
}
float V_SmithGGXCorrelatedFast(const float NdotV, const float NdotL, const float roughness)
{
	float ggxV = NdotL * (NdotV * (1.0 - roughness) + roughness);
	float ggxL = NdotV * (NdotL * (1.0 - roughness) + roughness);
	return 0.5 / max(ggxV + ggxL, 1e-5);
}
vec3 FresnelSchlickRoughness(float HdotV, vec3 F0, float roughness)
{
	float x = 1.0 - HdotV;
	float x2 = x * x;
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * (x2 * x2 * x);
}
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
vec3 doPointLightSpecular(vec3 lightRadiance, vec3 F, float NdotL, float NdotV, float NdotH, float roughness, float roughnessSq)
{
	float NDF = DistributionGGX(NdotH, roughnessSq);
	float Vis = V_SmithGGXCorrelatedFast(NdotV, NdotL, roughness);
	return (NDF * Vis * F) * lightRadiance * NdotL;
}
vec3 doAreaLightSpecular(vec3 lightRadiance, vec3 Lspec, vec3 V, vec3 N, vec3 specularCol, float lightSize, float distSpec, float roughness, float roughnessSq)
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
vec3 doLight(vec3 lightRadiance, vec3 L, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
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
vec3 doPointLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	vec3 lightVec = light.pos - pos;
	float dist = length(lightVec);
	vec3 L = lightVec / dist;
	float falloff = squareFalloff(dist, light.range);
	vec3 lightRadiance = light.color * falloff;
	return doLight(lightRadiance, L, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
}
vec3 doSpotLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
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
vec3 doAreaLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	float height = length(light.direction);
	if (height < 1e-5 || light.width < 1e-5)
		return vec3(0.0);

	vec3 up     = light.direction / height;
	vec3 ref    = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 right0 = normalize(cross(up, ref));
	float cs    = cos(light.rotation);
	float sn    = sin(light.rotation);
	vec3 right  = right0 * cs + cross(up, right0) * sn;
	vec3 quadNormal  = cross(up, right);
	float halfWidth  = light.width * 0.5;
	float halfHeight = height * 0.5;
	float lightSize  = (halfWidth + halfHeight) * 0.5;
	vec3 center      = light.pos;

	// One-sided emitter: ignore fragments behind the quad.
	if (dot(pos - center, quadNormal) <= 0.0)
		return vec3(0.0);

	// ---- Diffuse -------------------------------------------------------------
	vec3 closest     = closestPointOnRect(pos, center, right, up, halfWidth, halfHeight);
	vec3 LdiffVec    = closest - pos;
	float distDiff   = length(LdiffVec);
	vec3 Ldiff       = LdiffVec / max(distDiff, 1e-5);
	float facingDiff = max(dot(quadNormal, -Ldiff), 0.0);

	// Horizon clamp: smoothstep the center N.L over the quad's angular half-extent.
	// As the quad clips below the horizon it fades out proportionally to its apparent size.
	float NdotLcenter = dot(N, normalize(center - pos));
	float halfExtent  = lightSize / max(distDiff, 1e-5);
	float horizonVis  = smoothstep(-halfExtent, halfExtent, NdotLcenter);
	vec3 diffRadiance = light.color * squareFalloff(distDiff, light.range) * facingDiff;
	vec3 Hdiff        = normalize(Ldiff + V);
	vec3 Fdiff        = FresnelSchlick(max(dot(Hdiff, V), 0.0), specularCol);
	vec3 diffuse      = doLightDiffuseOnly(diffRadiance, Fdiff, matColOverPi, metalness, horizonVis);

	// ---- Specular (representative point) -------------------------------------
	// Intersect the mirror ray with the quad plane and clamp to the rectangle, then shade from
	// that point with its own distance and facing (skipped for rough surfaces: lobe is broad).
	vec3 Lspec;
	float distSpec;
	vec3 specRadiance;
	if (roughness < 0.6)
	{
		vec3 specPoint = closest;
		vec3 R  = reflect(-V, N);
		float d = dot(R, quadNormal);
		if (d < 0.0)
		{
			float t = dot(center - pos, quadNormal) / d;
			if (t > 0.0)
				specPoint = closestPointOnRect(pos + R * t, center, right, up, halfWidth, halfHeight);
		}

		vec3 LspecVec = specPoint - pos;
		distSpec      = max(length(LspecVec), 1e-4);
		Lspec         = LspecVec / distSpec;

		float facingSpec = max(dot(quadNormal, -Lspec), 0.0);
		specRadiance = light.color * squareFalloff(distSpec, light.range) * facingSpec;
	}
	else
	{
		Lspec        = Ldiff;
		distSpec     = distDiff;
		specRadiance = diffRadiance;
	}
	// Widen and renormalize the lobe by the quad's apparent size (lightSize) so the highlight spreads
	// out and dims as the light gets close to the surface, instead of staying a tiny punctual spike.
	vec3 specular = doAreaLightSpecular(specRadiance, Lspec, V, N, specularCol, lightSize, distSpec, roughness, roughnessSq);

	return diffuse + specular;
}
vec3 doTubeLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	float height   = length(light.direction);
	float halfLen  = height * 0.5;
	float radius   = abs(light.width);
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
		float t      = (abs(denom) > 1e-6) ? clamp((dot(R, l0) * RdotLd - dot(l0, ld)) / denom, 0.0, 1.0) : 0.5;
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
vec3 doSunLight(vec3 worldPos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	vec3 L = normalize(u_sunDirection.xyz);
	if (max(dot(N, L), 0.0) <= 0.0)
		return vec3(0.0);
	float visibility = sampleSunShadow(worldPos, N);
	vec3 lightRadiance = u_sunColor.rgb * visibility;
	return doLight(lightRadiance, L, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
}
vec3 doLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
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
	const uint16_t materialIdx   = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
	const MaterialInfo material  = in_materialInfos[materialIdx];
	const uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);
	const uint16_t normalTexIdx  = uint16_t((material.diffuseNormalTexIdx & 0xFFFF0000) >> 16);
	const uint16_t metalRoughnessTexIdx = uint16_t(material.metalRoughnessTexIdxAlphaMode & 0x0000FFFF);
	const uint16_t alphaMode     = uint16_t((material.metalRoughnessTexIdxAlphaMode & 0xFFFF0000) >> 16);

	float roughness = 0.65;
	float metalness = 0.0;
	if (metalRoughnessTexIdx != uint16_t(0xFFFF))
	{
		const vec2 metalRoughness = texture(u_textures[metalRoughnessTexIdx], in_uv).bg;
		metalness = metalRoughness.x;
		roughness = max(metalRoughness.y, 0.01);
	}

	const vec3 V  = normalize(u_viewPos - in_pos);
	const vec2 uv = in_uv; //spomDisplaceUV(uint(normalTexIdx), V);

	const vec4 diffuseSample  = texture(u_textures[diffuseTexIdx], uv);
	if (alphaMode == ALPHA_MODE_MASK && diffuseSample.a < material.opacity)
		discard;
	const vec3 materialColor  = diffuseSample.xyz;
	const vec3 materialNormal = texture(u_textures[normalTexIdx], uv).xyz;
	const vec3 specularColor  = mix(vec3(0.04), materialColor, metalness);

	const vec3 N = in_tbn * normalize(materialNormal * 2.0 - 1.0);

	const float roughnessSq = roughness * roughness;
	const vec3 matColOverPi = materialColor / PI;
	
	// Diffuse global illumination: sample the ray-traced SH irradiance probes from the fixed volume.
	// evalProbeSH returns the cosine-convolved irradiance E(N); Lambertian indirect = albedo/PI * E.
	// Falls back to a small flat ambient outside the probe volume.
	const vec3 indirectE = evalProbeSH(in_pos, N, u_giVolumeMin.xyz);
	vec3 color = materialColor * (indirectE / PI);//(indirectE.x >= 0.0) ? materialColor * (indirectE / PI) : materialColor * 0.10;

	color += doSunLight(in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
	//color = mix(color, cascadeDebugColor(getSunCascade(in_pos)), 0.35);
	const ivec3 gridPos = getGridPos(in_pos);
    uint tableIdx = getTableIdx(gridPos);
	
	while (true)
	{
		const uint gridIdx = getGridIdx(tableIdx);
		if (gridIdx == EMPTY_ENTRY)
			break;
		const ivec3 gridMin = getGridMin(gridIdx);
		if (gridMin == gridPos)
		{
			const uint numLargeLights = getLargeLightCount(gridIdx);
			for (uint i = 0; i < min(numLargeLights, MAX_LARGE_LIGHTS_PER_GRID); ++i)
			{
				const uint lightId    = getLargeLightId(gridIdx, i);
				const LightInfo light = in_lightInfos[lightId];
				color += doLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
			}

			const uint cellOffset = calcCellOffset(gridIdx, gridMin, in_pos);
			const uint numLights  = getNumLightsForCell(cellOffset);
			for (uint i = 0; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
			{
				const uint lightId    = getLightId(cellOffset, i);
				const LightInfo light = in_lightInfos[lightId];
				color += doLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
			}
			break;
		}
		tableIdx = getNextTableIdx(tableIdx);
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
// Visualize cascades
// color = mix(color, cascadeDebugColor(getSunCascade(in_pos)), 0.35);