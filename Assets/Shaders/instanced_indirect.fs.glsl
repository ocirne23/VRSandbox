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

#include "punctual_lights.inc.glsl"

vec3 doSunLight(vec3 worldPos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	vec3 L = normalize(u_sunDirection.xyz);
	if (max(dot(N, L), 0.0) <= 0.0)
		return vec3(0.0);
	float visibility = u_rtSunShadow > 0.5 ? traceSunVisibility(worldPos, N) : sampleSunShadow(worldPos, N);
	vec3 lightRadiance = atmosTransmittanceToLight(0.0, L, u_skyUp) * u_sunColor.rgb * visibility * u_eclipseParams.x;
	return doLight(lightRadiance, L, V, N, specularCol, matColOverPi, metalness, roughness, roughnessSq);
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

#ifdef TERRAIN
// Procedural terrain surface color from world height + slope. No textures: procedural terrain chunks
// carry none (the material resolves to the shared fallback). NOTE: the height bands assume the default
// TerrainConfig (sea level ~0, continent/mountain amplitudes) — a later pass should feed sea level /
// height scale through the UBO so this tracks the tweaks. Slope reads bare rock on steep faces.
vec3 terrainAlbedo(vec3 worldPos, vec3 N)
{
	const vec3 sand  = vec3(0.76, 0.70, 0.50);
	const vec3 grass = vec3(0.26, 0.42, 0.17);
	const vec3 rock  = vec3(0.37, 0.34, 0.31);
	const vec3 snow  = vec3(0.90, 0.93, 0.97);

	const float h = worldPos.y;
	const float slope = 1.0 - clamp(N.y, 0.0, 1.0);

	// Cheap value noise to break up the otherwise flat height bands.
	const float n = fract(sin(dot(floor(worldPos.xz * 0.08), vec2(12.9898, 78.233))) * 43758.5453) * 2.0 - 1.0;

	vec3 col = mix(sand, grass, smoothstep(0.5, 6.0 + n * 2.0, h));
	col = mix(col, rock, smoothstep(55.0 + n * 8.0, 110.0, h));
	col = mix(col, snow, smoothstep(135.0 + n * 10.0, 185.0, h));
	col = mix(col, rock, smoothstep(0.45, 0.70, slope)); // steep faces -> bare rock
	return col;
}
#endif

void main()
{
#ifdef STEREO
	g_viewIndex = int(u_viewIndex); // per-eye reconstruction (AO upsample) + view pos
#endif
	const vec3 V  = normalize(u_viewPos - in_pos);
	vec3  materialColor;
	vec3  N;
	float roughness;
	float metalness;

#ifdef TERRAIN
	// Geometric (interpolated vertex) normal — no normal map for terrain.
	N = normalize(in_tbn[2]);
	materialColor = terrainAlbedo(in_pos, N);
	roughness = 0.92;
	metalness = 0.0;
#else
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

	roughness = 0.65;
	metalness = 0.0;
	if (metalRoughnessTexIdx != uint16_t(0xFFFF))
	{
		const vec2 metalRoughness = texture(u_textures[metalRoughnessTexIdx], uv).bg;
		metalness = metalRoughness.x;
		roughness = max(metalRoughness.y, 0.01);
	}

	materialColor = diffuseSample.xyz;
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
	N = normalize(in_tbn * tangentNormal);
#endif

	const vec3 specularColor  = mix(vec3(0.04), materialColor, metalness);
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
	const vec3 indirect = ((indirectE.x >= 0.0) ? (indirectE / PI) : skyRadiance(bentN)) * u_aoParams.y;
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

#ifdef TERRAIN
	out_color = vec4(color, 1.0); // opaque terrain
#else
	out_color = vec4(color, min(diffuseSample.a, material.opacity));
#endif
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