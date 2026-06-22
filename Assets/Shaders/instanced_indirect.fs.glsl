#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_query : enable
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

layout (binding = 18) uniform sampler2D u_textures[]; // highest binding in the set: variable descriptor count
layout (binding = 8) uniform sampler2DArrayShadow u_shadowMap;      // comparison sampler (hardware PCF)
layout (binding = 9) uniform sampler2DArray u_shadowMapDepth;       // raw depth (PCSS blocker search)
layout (binding = 13) uniform sampler2D u_ao;                       // denoised half-res screen-space AO (bilateral upsample)
layout (binding = 12) uniform sampler2D u_gbufferDepth;             // full-res hardware depth (AO upsample edge weights)

layout (binding = 11) uniform accelerationStructureEXT u_tlas;       // ray-traced shadows for punctual/area lights

// Mesh/instance data for the shadow rays' alpha test (rt_shadow.inc.glsl). in_instances must be the same
// buffer the TLAS instance records were built from (custom index = index into it), not the culled list.
struct InMeshInfo
{
	vec3 center;
	float radius;
	uint indexCount;
	uint firstIndex;
	int  vertexOffset;
	uint _padding;
};
struct InMeshInstance
{
	uint renderNodeIdx;
	uint instanceOffsetIdx;
	uint meshIdxMaterialIdx;
	uint pipelineIdxAlphaMode;
};
layout (binding = 14, std430) readonly buffer InRTVertices  { float in_vertices[]; }; // MeshVertex as 12 floats
layout (binding = 15, std430) readonly buffer InRTIndices   { uint in_indices[]; };
layout (binding = 16, std430) readonly buffer InRTMeshInfos { InMeshInfo in_meshInfos[]; };
layout (binding = 17, std430) readonly buffer InRTInstances { InMeshInstance in_instances[]; };

// GI irradiance probes (diffuse indirect). Persistent cascaded clipmap SH volume, written by the probe
// trace pass; addressed toroidally relative to the camera (u_viewPos), so no hash table is needed.
layout (binding = 10, std430) readonly buffer GiGridData { float gi_gridData[]; };

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;
#ifdef STEREO
layout (push_constant) uniform ViewPC { uint u_viewIndex; }; // selects the per-eye view (1=left, 2=right) in VR
#endif

layout (location = 0) out vec4 out_color;

#include "shadows.inc.glsl"

#define GRID_DATA_NAME         in_gridData
#define GRID_TABLE_NAME 	   in_gridTable
#define TABLE_SIZE_NAME        in_tableSize
#include "light_grid.inc.glsl"

#define GI_GRID_DATA_NAME      gi_gridData
#include "gi_probe.inc.glsl"

#include "rt_shadow.inc.glsl"

// Hard ray-traced visibility toward a point on the light; tMax stops just short of the target so the
// light's own position never registers as a hit. Alpha-masked geometry is alpha-tested.
float traceLightVisibility(vec3 pos, vec3 N, vec3 target)
{
	vec3 toLight = target - pos;
	float dist = length(toLight);
	return rtShadowVisibility(pos + N * 0.02, toLight / dist, 0.01, dist - 0.02);
}
uint hashU(uint x)
{
	x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
	return x;
}
// Spatiotemporal blue-noise-style jitter. The per-pixel seed is *static* across frames (white noise,
// spatially decorrelated between neighbours); each pixel's offset is then advanced along a low-discrepancy
// golden-ratio sequence per frame. So the TAA history window accumulates a well-stratified set per pixel
// (fast, low-variance convergence) instead of N independent white-noise draws. The temporal increment is a
// distinct irrational from R2_OFFSET (the within-frame per-ray stride) so the two sequences don't resonate.
#define SHADOW_TEMPORAL_OFFSET vec2(0.5545497, 0.3083158)
vec2 shadowJitter()
{
	const uint seed = hashU(uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u);
	const vec2 base = vec2(float(seed & 0x00FFFFFFu), float(hashU(seed) & 0x00FFFFFFu)) / float(0x01000000u);
	return fract(base + float(u_frameIndex) * SHADOW_TEMPORAL_OFFSET);
}
// 1-4 rays based on the light's apparent size: penumbra noise only matters when the emitter subtends a
// large solid angle, so distant/small lights stay at a single ray.
uint shadowRayCount(float lightSize, float dist)
{
	return clamp(uint(lightSize / max(dist, 1e-4) * 8.0), 1u, 4u);
}
// R2 additive recurrence offsets successive rays so they stratify over the emitter within one frame.
#define R2_OFFSET vec2(0.7548777, 0.5698403)
// u_sunShadowRays rays jittered within the sun's angular cone (u_sunAngularCos), stratified across the
// disc with the R2 recurrence; TAA resolves the remaining noise over time. More rays smooth out the
// IGN's structured pattern when the disc is wide.
float traceSunVisibility(vec3 pos, vec3 N)
{
	const vec3 L = normalize(u_sunDirection.xyz);
	const vec3 ref = abs(L.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	const vec3 T = normalize(cross(ref, L));
	const vec3 B = cross(L, T);
	const vec3 origin = pos + N * 0.02;

	const uint numRays = clamp(uint(u_sunShadowRays), 1u, 8u);
	const vec2 jitter = shadowJitter();
	float vis = 0.0;
	for (uint i = 0u; i < numRays; ++i)
	{
		const vec2 u = fract(jitter + R2_OFFSET * float(i));
		const float cosTheta = mix(u_sunAngularCos, 1.0, u.x); // uniform over the spherical cap
		const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
		const float phi = 6.2831853 * u.y;
		const vec3 dir = T * (sinTheta * cos(phi)) + B * (sinTheta * sin(phi)) + L * cosTheta;
		vis += rtShadowVisibility(origin, dir, 0.01, 10000.0);
	}
	return vis / float(numRays);
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
void areaLightBasis(LightInfo light, out vec3 right, out vec3 up, out float halfWidth, out float halfHeight)
{
	float height = length(light.direction);
	up          = light.direction / height;
	vec3 ref    = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 right0 = normalize(cross(up, ref));
	float cs    = cos(light.rotation);
	float sn    = sin(light.rotation);
	right       = right0 * cs + cross(up, right0) * sn;
	halfWidth   = light.width * 0.5;
	halfHeight  = height * 0.5;
}
vec3 doAreaLight(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	vec3 right, up;
	float halfWidth, halfHeight;
	areaLightBasis(light, right, up, halfWidth, halfHeight);
	vec3 quadNormal  = cross(up, right);
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
	float visibility = u_rtSunShadow > 0.5 ? traceSunVisibility(worldPos, N) : sampleSunShadow(worldPos, N);
	vec3 lightRadiance = atmosTransmittanceToLight(0.0, L, u_skyUp) * u_sunColor.rgb * visibility * u_eclipseParams.x;
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
float areaLightVisibility(LightInfo light, vec3 pos, vec3 N)
{
	vec3 right, up;
	float halfWidth, halfHeight;
	areaLightBasis(light, right, up, halfWidth, halfHeight);
	const uint numRays = shadowRayCount(halfWidth + halfHeight, distance(pos, light.pos));
	const vec2 jitter = shadowJitter();
	float vis = 0.0;
	for (uint i = 0u; i < numRays; ++i)
	{
		vec2 u = fract(jitter + R2_OFFSET * float(i)) * 2.0 - 1.0;
		vis += traceLightVisibility(pos, N, light.pos + right * (u.x * halfWidth) + up * (u.y * halfHeight));
	}
	return vis / float(numRays);
}
float tubeLightVisibility(LightInfo light, vec3 pos, vec3 N)
{
	float height  = length(light.direction);
	vec3 axis     = light.direction / height;
	float halfLen = height * 0.5;
	float radius  = abs(light.width);
	// Sample the visible silhouette: jitter along the axis, plus a perpendicular offset within the plane
	// facing the shaded point (the radial extent the surface actually sees).
	vec3 toPos = pos - light.pos;
	vec3 side  = cross(axis, toPos);
	float sideLen = length(side);
	side = sideLen > 1e-5 ? side / sideLen : vec3(0.0);
	const uint numRays = shadowRayCount(halfLen + radius, length(toPos));
	const vec2 jitter = shadowJitter();
	float vis = 0.0;
	for (uint i = 0u; i < numRays; ++i)
	{
		vec2 u = fract(jitter + R2_OFFSET * float(i)) * 2.0 - 1.0;
		vis += traceLightVisibility(pos, N, light.pos + axis * (u.x * halfLen) + side * (u.y * radius));
	}
	return vis / float(numRays);
}
vec3 doLightShadowed(LightInfo light, vec3 pos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	vec3 lit = doLight(light, pos, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
	if (u_rtLightShadows < 0.5 || dot(lit, lit) <= 1e-7) // toggle off, or black analytic term: skip the trace
		return lit;
	if (light.width > 0.0)
		return lit * (light.range < 0.0 ? tubeLightVisibility(light, pos, N) : areaLightVisibility(light, pos, N));
	return lit * traceLightVisibility(pos, N, light.pos); // point/spot: genuinely punctual, one center ray
}

// Depth-aware 2x2 upsample of the half-res AO/bent-normal image. Plain bilinear bleeds across depth
// discontinuities (a far wall's AO/bent normal mixing into a near silhouette shows as a bright GI rim),
// so each tap's bilinear weight is scaled by its world-space distance to the shaded point. Tap depths
// come from the full-res depth at the tap's UV (the half-res texel center), which is close enough to
// the depth the trace actually used.
vec4 sampleAOBilateral(vec2 fullUv, vec3 pos)
{
	const vec2 aoRes   = ceil(u_screenSize.xy * 0.5);
	const vec2 aoTexel = 1.0 / aoRes;
	const vec2 st   = fullUv * aoRes - 0.5;
	const vec2 base = (floor(st) + 0.5) * aoTexel;
	const vec2 f    = fract(st);
	const float bw[4] = float[]((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	const vec2 offs[4] = vec2[](vec2(0.0), vec2(aoTexel.x, 0.0), vec2(0.0, aoTexel.y), aoTexel);

	const float sigmaZ = max(0.05 * length(pos - u_viewPos), 0.02);
	vec4 sum = vec4(0.0);
	float wsum = 0.0;
	for (int i = 0; i < 4; ++i)
	{
		const vec2 uv = base + offs[i];
		const float d = texture(u_gbufferDepth, uv).r;
		if (d >= 1.0)
			continue;
		const vec3 tapPos = worldPosFromDepth(uv, d);
		const float dz = length(tapPos - pos);
		const float w  = bw[i] * exp(-(dz * dz) / (2.0 * sigmaZ * sigmaZ));
		sum  += texture(u_ao, uv) * w;
		wsum += w;
	}
	// All taps rejected (thin geometry the half-res image never saw): fall back to plain bilinear.
	return wsum > 1e-4 ? sum / wsum : texture(u_ao, fullUv);
}

void main()
{
#ifdef STEREO
	g_viewIndex = int(u_viewIndex); // per-eye reconstruction (AO upsample) + view pos
#endif
	const uint16_t materialIdx   = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
	const MaterialInfo material  = in_materialInfos[materialIdx];
	const uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);
	const uint16_t normalTexIdx  = uint16_t((material.diffuseNormalTexIdx & 0xFFFF0000) >> 16);
	const uint16_t metalRoughnessTexIdx = uint16_t(material.metalRoughnessTexIdxAlphaMode & 0x0000FFFF);
	const uint16_t alphaMode     = uint16_t((material.metalRoughnessTexIdxAlphaMode & 0xFFFF0000) >> 16);
	const vec2 uv = in_uv; //spomDisplaceUV(uint(normalTexIdx), V);

	const vec4 diffuseSample  = texture(u_textures[diffuseTexIdx], uv);
	if (alphaMode == ALPHA_MODE_MASK && diffuseSample.a < material.opacity)
		discard;

	float roughness = 0.65;
	float metalness = 0.0;
	if (metalRoughnessTexIdx != uint16_t(0xFFFF))
	{
		const vec2 metalRoughness = texture(u_textures[metalRoughnessTexIdx], uv).bg;
		metalness = metalRoughness.x;
		roughness = max(metalRoughness.y, 0.01);
	}

	const vec3 V  = normalize(u_viewPos - in_pos);
	const vec3 materialColor  = diffuseSample.xyz;
	const vec3 specularColor  = mix(vec3(0.04), materialColor, metalness);
	// Two-channel BC5 normal maps store only X/Y (red/green), so .z reads 0 and would flip the normal
	// into the surface — reconstruct Z from X/Y. Full RGB(A) normal maps keep their stored Z.
	const vec3 normalSample = texture(u_textures[normalTexIdx], uv).xyz;
	vec3 tangentNormal;
	if ((material.flags & MATERIAL_FLAG_BC5_NORMAL) != 0u)
	{
		const vec2 normalXY = normalSample.xy * 2.0 - 1.0;
		tangentNormal = vec3(normalXY, sqrt(max(1.0 - dot(normalXY, normalXY), 0.0)));
	}
	else
	{
		tangentNormal = normalize(normalSample * 2.0 - 1.0);
	}
	const vec3 N = normalize(in_tbn * tangentNormal);

	const float roughnessSq = roughness * roughness;
	const vec3 matColOverPi = materialColor / PI;
	
	float ao = 1.0;
	vec3 bentN = N;
	if (u_aoParams.x > 0.5)
	{
		const vec4 aoSample = sampleAOBilateral(gl_FragCoord.xy * u_screenSize.zw, in_pos);
		ao = aoSample.w;
		// Evaluate the indirect irradiance along the bent normal rather than the surface normal: in concave
		// areas it points toward the open hemisphere, so the low-frequency probe SH stops leaking light from
		// occluded directions. Mix partway toward N so flat, unoccluded surfaces are left untouched.
		bentN = normalize(mix(N, normalize(aoSample.xyz), 0.75));
	}
	const vec3 indirectE = evalProbeSH(in_pos, bentN);
	const vec3 indirect = (indirectE.x >= 0.0) ? (indirectE / PI) : skyRadiance(bentN);
	vec3 color = materialColor * (indirect + u_ambientColor) * ao;

	color += doSunLight(in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
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
				color += doLightShadowed(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
			}

			const uint cellOffset = calcCellOffset(gridIdx, gridMin, in_pos);
			const uint numLights  = getNumLightsForCell(cellOffset);
			for (uint i = 0; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
			{
				const uint lightId    = getLightId(cellOffset, i);
				const LightInfo light = in_lightInfos[lightId];
				color += doLightShadowed(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
			}
			break;
		}
		tableIdx = getNextTableIdx(tableIdx);
	}

	out_color = vec4(color, min(diffuseSample.a, material.opacity));
}

//	#define GI_DEBUG_VIZ 0
//	#if GI_DEBUG_VIZ == 1
//		color = giDebugColor(in_pos, N);
//	#elif GI_DEBUG_VIZ == 2
//		color = (indirectE.x >= 0.0) ? indirectE / PI : vec3(0.0);
//	#endif
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