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
    vec4 u_sunDirection;  // xyz = normalized direction towards the sun
    vec4 u_sunColor;      // rgb = color * intensity
    mat4 u_cascadeViewProj[NUM_SHADOW_CASCADES]; // m[0][3] = far distance, m[1][3] = texel size (see cascade* below)
    vec4 u_shadowParams;  // x = depth bias, y = normal bias (texels), z = 1/resolution, w = pcf radius
};

// Per-cascade scalars are stashed in the matrix's bottom row; restore [0,0,0,1] before using it.
float cascadeSplit(int c) { return u_cascadeViewProj[c][0][3]; }
float cascadeTexelWorldSize(int c) { return u_cascadeViewProj[c][1][3]; }
mat4 cascadeMatrix(int c)
{
	mat4 m = u_cascadeViewProj[c];
	m[0][3] = 0.0; m[1][3] = 0.0; m[2][3] = 0.0; m[3][3] = 1.0;
	return m;
}
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

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec4 out_color;

const float PI = 3.14159265359;

ivec3 getGridMin(uint gridIdx)        { return ivec3(in_gridData[gridIdx + 0], in_gridData[gridIdx + 1], in_gridData[gridIdx + 2]); }
uint getCellSize(uint gridIdx)        { return in_gridData[gridIdx + 3]; }
ivec3 getGridPos(vec3 pos)            { return ivec3(floor(pos / GRID_SIZE)); }
uint getLargeLightCount(uint gridIdx) { return in_gridData[gridIdx + 4]; }
uint getCellOffset(uint gridIdx, uint cellIdx) { return gridIdx + 8 + cellIdx * (MAX_LIGHTCELL_LIGHTS / 2 + 1); }
uint getLightId(uint cellOffset, uint lightIdx) // return as uint instead of uint16 to avoid driver bug (treating uint16_t as signed)
{
    const uint packed = in_gridData[cellOffset + 1 + lightIdx / 2];
	return (lightIdx & 1u) == 0u ? uint(packed & 0x0000FFFF) : uint(packed >> 16);
}
uint getLargeLightId(uint gridIdx, uint lightIdx)
{
	const uint packed = in_gridData[gridIdx + 5 + lightIdx / 2];
	return (lightIdx & 1u) == 0u ? uint(packed & 0x0000FFFF) : uint(packed >> 16);
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
int getSunCascade(vec3 worldPos)
{
	float dist = length(worldPos - u_viewPos);
	for (int i = 0; i < NUM_SHADOW_CASCADES; ++i)
		if (dist < cascadeSplit(i)) return i;
	return NUM_SHADOW_CASCADES - 1;
}

// Fraction of a cascade's far range over which it cross-fades into the next cascade.
#define SHADOW_CASCADE_BLEND 0.25
#define GOLDEN_RATIO_FRACT 0.6180339887
#define PCSS_SUN_SIZE 200.0            // penumbra texels per unit normalized depth gap (~2*tan(sunRadius)*res); bigger = softer
#define PCSS_MAX_PENUMBRA_TEXELS 15.0  // cap so the kernel never gets too sparse/noisy
#define PCSS_MIN_PENUMBRA_TEXELS 1.0   // floor so contact shadows stay crisp
#define PCSS_BLOCKER_SAMPLES 16
#define PCSS_FILTER_SAMPLES 16
#define GOLDEN_ANGLE 2.39996323

// Interleaved gradient noise: a cheap per-pixel value in [0,1) for rotating the sample kernel.
float interleavedGradientNoise(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}
// Evenly distributed disk sample (golden-angle spiral), rotated per pixel. Far better coverage than a
// fixed 12-point Poisson set when the kernel radius is large, which keeps PCSS smooth at low tap counts.
vec2 vogelDisk(int i, int count, float rotation)
{
	float r = sqrt((float(i) + 0.5) / float(count));
	float theta = float(i) * GOLDEN_ANGLE + rotation;
	return r * vec2(cos(theta), sin(theta));
}
// PCSS blocker search: average the depths of texels closer to the light than the receiver. Returns the
// blocker count (0 => no occluders => fully lit).
float blockerSearch(vec2 uv, int cascade, float receiverDepth, float radiusUV, float rotation, out float avgBlocker)
{
	float sum = 0.0;
	float count = 0.0;
	for (int i = 0; i < PCSS_BLOCKER_SAMPLES; ++i)
	{
		vec2 off = vogelDisk(i, PCSS_BLOCKER_SAMPLES, rotation) * radiusUV;
		float d = textureLod(u_shadowMapDepth, vec3(uv + off, float(cascade)), 0.0).r;
		if (d < receiverDepth)
		{
			sum += d;
			count += 1.0;
		}
	}
	avgBlocker = (count > 0.0) ? sum / count : 0.0;
	return count;
}
// Projects a world position into one cascade's shadow space. Returns (uv, biased depth, valid flag).
// The receiver is offset along its normal by normalOffset (world units) and the compare depth is
// nudged by depthBias; both are scaled per-cascade and by surface slope at the call site.
vec4 projectCascade(vec3 worldPos, vec3 N, int cascade, float normalOffset, float depthBias)
{
	vec4 lightPos = cascadeMatrix(cascade) * vec4(worldPos + N * normalOffset, 1.0);
	vec3 proj = lightPos.xyz / lightPos.w;
	vec2 uv = proj.xy * 0.5 + 0.5;
	float valid = (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) || proj.z > 1.0) ? 0.0 : 1.0;
	return vec4(uv, proj.z - depthBias, valid);
}
// One PCF tap. Stochastically reads cascade A or B based on a per-tap dither vs the blend factor t,
// so the boundary cross-fades without adding samples. Out-of-bounds taps read as lit.
float shadowTap(vec4 pa, vec4 pb, int ca, int cb, vec2 off, float dither, float t)
{
	bool useB = dither < t;
	vec4 p = useB ? pb : pa;
	if (p.w < 0.5)
		return 1.0;
	return texture(u_shadowMap, vec4(p.xy + off, float(useB ? cb : ca), p.z));
}
// Variable-radius PCF over a Vogel disk for a single cascade.
float pcfVogelSingle(vec4 p, int cascade, float radiusUV, float rotation)
{
	float sum = 0.0;
	for (int i = 0; i < PCSS_FILTER_SAMPLES; ++i)
	{
		vec2 off = vogelDisk(i, PCSS_FILTER_SAMPLES, rotation) * radiusUV;
		sum += texture(u_shadowMap, vec4(p.xy + off, float(cascade), p.z));
	}
	return sum / float(PCSS_FILTER_SAMPLES);
}
// Full PCSS for one cascade: blocker search -> penumbra estimate -> variable-radius PCF. Returns
// visibility in [0,1]; fragments outside the cascade or with no occluders read as fully lit.
float pcssCascade(vec4 p, int cascade, float texelUV, float rotation)
{
	if (p.w < 0.5)
		return 1.0; // outside this cascade's coverage
	float searchRadiusUV = PCSS_MAX_PENUMBRA_TEXELS * texelUV;
	float avgBlocker;
	float blockers = blockerSearch(p.xy, cascade, p.z, searchRadiusUV, rotation, avgBlocker);
	if (blockers <= 0.0)
		return 1.0; // no occluders found
	// Directional penumbra: width grows with the world gap to the blocker (constant across cascades
	// once expressed in texels). Caster touching the surface => ~MIN texels (sharp); far => up to MAX.
	float penumbraTexels = clamp((p.z - avgBlocker) * PCSS_SUN_SIZE, PCSS_MIN_PENUMBRA_TEXELS, PCSS_MAX_PENUMBRA_TEXELS);
	return pcfVogelSingle(p, cascade, penumbraTexels * texelUV, rotation);
}
// Border PCSS: the fixed tap budget (blocker + filter) is split between the two cascades by t, so a
// border pixel costs the same as a normal one. Each tap is routed to a cascade by a deterministic
// low-discrepancy key, keeping both subsets spatially uniform; each cascade's visibility is averaged
// over its own taps, then blended by the continuous factor t.
float pcssBorder(vec4 pa, vec4 pb, int ca, int cb, float texelUV, float rotation, float t)
{
	float searchRadiusUV = PCSS_MAX_PENUMBRA_TEXELS * texelUV;
	float minRadiusUV = PCSS_MIN_PENUMBRA_TEXELS * texelUV;
	bool validA = pa.w >= 0.5; // fragment lies inside this cascade's coverage
	bool validB = pb.w >= 0.5;
	if (!validA && !validB)
		return 1.0; // outside both cascades: no shadow data

	// Blocker search, split between cascades (taps routed to an invalid cascade are skipped).
	float sumDA = 0.0, nDA = 0.0, sumDB = 0.0, nDB = 0.0;
	for (int i = 0; i < PCSS_BLOCKER_SAMPLES; ++i)
	{
		bool useB = fract(float(i) * GOLDEN_RATIO_FRACT) < t;
		if ((useB && !validB) || (!useB && !validA))
			continue;
		vec4 p = useB ? pb : pa;
		vec2 off = vogelDisk(i, PCSS_BLOCKER_SAMPLES, rotation) * searchRadiusUV;
		float d = textureLod(u_shadowMapDepth, vec3(p.xy + off, float(useB ? cb : ca)), 0.0).r;
		if (d < p.z)
		{
			if (useB) { sumDB += d; nDB += 1.0; }
			else      { sumDA += d; nDA += 1.0; }
		}
	}
	// The blocker search only SIZES the penumbra; it never decides lit vs shadowed (the PCF does). With
	// the tap budget split, a cascade can easily find zero blockers even in shadow, so fall back to the
	// minimum (sharp) radius rather than declaring it lit -- otherwise the PCF is skipped and a bright
	// line appears between two fully-shadowed cascades.
	float radA = (nDA > 0.0) ? clamp((pa.z - sumDA / nDA) * PCSS_SUN_SIZE, PCSS_MIN_PENUMBRA_TEXELS, PCSS_MAX_PENUMBRA_TEXELS) * texelUV : minRadiusUV;
	float radB = (nDB > 0.0) ? clamp((pb.z - sumDB / nDB) * PCSS_SUN_SIZE, PCSS_MIN_PENUMBRA_TEXELS, PCSS_MAX_PENUMBRA_TEXELS) * texelUV : minRadiusUV;

	// Filter, split between cascades; the PCF determines visibility for every tap.
	float sumA = 0.0, nA = 0.0, sumB = 0.0, nB = 0.0;
	for (int i = 0; i < PCSS_FILTER_SAMPLES; ++i)
	{
		bool useB = fract(float(i) * GOLDEN_RATIO_FRACT) < t;
		if ((useB && !validB) || (!useB && !validA))
			continue; // route only to valid cascades; the other supplies the result via fallback below
		vec4 p = useB ? pb : pa;
		float rad = useB ? radB : radA;
		float vis = texture(u_shadowMap, vec4(p.xy + vogelDisk(i, PCSS_FILTER_SAMPLES, rotation) * rad, float(useB ? cb : ca), p.z));
		if (useB) { sumB += vis; nB += 1.0; }
		else      { sumA += vis; nA += 1.0; }
	}
	// If one cascade is invalid (or got no taps), use the other's visibility instead of injecting light.
	if (!validA || nA == 0.0) return sumB / nB;
	if (!validB || nB == 0.0) return sumA / nA;
	return mix(sumA / nA, sumB / nB, t);
}
// Selects the cascade, computes a cross-fade factor into the next one, and returns sun visibility.
float sampleSunShadow(vec3 worldPos, vec3 N)
{
	int cascade = getSunCascade(worldPos);
	int nextCascade = min(cascade + 1, NUM_SHADOW_CASCADES - 1);

	// Blend factor t ramps 0->1 across the last SHADOW_CASCADE_BLEND fraction of this cascade's range.
	float dist = length(worldPos - u_viewPos);
	float splitFar = cascadeSplit(cascade);
	float prevSplit = (cascade > 0) ? cascadeSplit(cascade - 1) : 0.0;
	float bandStart = mix(prevSplit, splitFar, 1.0 - SHADOW_CASCADE_BLEND);
	float t = (cascade == nextCascade) ? 0.0 : clamp((dist - bandStart) / max(splitFar - bandStart, 1e-4), 0.0, 1.0);

	// Slope factor (tan of the angle between N and the sun): widen bias at grazing angles where acne
	// and leaking appear, leaving flat-lit surfaces lightly biased. The normal offset is additionally
	// scaled by each cascade's world texel size so near/far cascades get a matching offset.
	vec3 L = normalize(u_sunDirection.xyz);
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	float slope = clamp(sqrt(1.0 - NdotL * NdotL) / max(NdotL, 1e-3), 1.0, 4.0);
	float depthBias = u_shadowParams.x * slope;
	float normalScale = u_shadowParams.y * slope;

	vec4 pa = projectCascade(worldPos, N, cascade, normalScale * cascadeTexelWorldSize(cascade), depthBias);
	float ditherBase = interleavedGradientNoise(gl_FragCoord.xy);

	float texelUV = u_shadowParams.z; // 1 / resolution
	float rotation = ditherBase * 6.2831853;
	// Outside the cross-fade band: one full-quality cascade evaluation. Inside it: split the same tap
	// budget across both cascades (constant cost) and blend by t.
	if (t <= 0.0)
		return pcssCascade(pa, cascade, texelUV, rotation);
	else
	{
		vec4 pb = projectCascade(worldPos, N, nextCascade, normalScale * cascadeTexelWorldSize(nextCascade), depthBias);
		return pcssBorder(pa, pb, cascade, nextCascade, texelUV, rotation, t);
	}
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
	
	const float ambient = 0.10f;
	vec3 color = materialColor * ambient;

	color += doSunLight(in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
	//color = mix(color, cascadeDebugColor(getSunCascade(in_pos)), 0.35);

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
				const uint lightId    = getLargeLightId(gridIdx, i);
				const LightInfo light = in_lightInfos[lightId];
				color += doLight(light, in_pos, V, N, specularColor, matColOverPi, metalness, roughness, roughnessSq);
			}
			
			const uint cellSize   = getCellSize(gridIdx);
			const uint numCells   = GRID_SIZE / cellSize;
			const ivec3 cellPos   = (ivec3(floor(in_pos)) - gridMin * GRID_SIZE) / int(cellSize);
			const uint numCellsSq = numCells * numCells;
			const uint cellIdx    = cellPos.x + cellPos.y * numCells + cellPos.z * numCellsSq;
			const uint cellOffset = getCellOffset(gridIdx, cellIdx);
			const uint numLights  = in_gridData[cellOffset];
			for (uint i = 0; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
			{
				const uint lightId    = getLightId(cellOffset, i);
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
// Visualize cascades
// color = mix(color, cascadeDebugColor(getSunCascade(in_pos)), 0.35);