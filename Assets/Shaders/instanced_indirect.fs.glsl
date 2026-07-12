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

layout (binding = 20) uniform sampler2D u_textures[]; // highest binding in the set: variable descriptor count
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

// Terrain data cascades (height/water level/climate/altitude) + FFT ocean maps: underwater sunlight
// (caustics) for EVERY lit material, and the TERRAIN variant's procedural coloring. Both bindings exist
// set-wide (19 = terrain data, 7 = u_oceanMaps — see StaticMeshGraphicsPipeline::buildPipelineLayout).
#define TERRAIN_HEIGHT_BINDING 19
#include "terrain_height.inc.glsl"
#define UNDERWATER_OCEAN_BINDING 7
#include "underwater_light.inc.glsl"

vec3 doSunLight(vec3 worldPos, vec3 V, vec3 N, vec3 specularCol, vec3 matColOverPi, float metalness, float roughness, float roughnessSq)
{
	vec3 L = normalize(u_sunDirection.xyz);
	if (max(dot(N, L), 0.0) <= 0.0)
		return vec3(0.0);
	float visibility = u_rtSunShadow > 0.5 ? traceSunVisibility(worldPos, N) : sampleSunShadow(worldPos, N);
	vec3 lightRadiance = atmosTransmittanceToLight(0.0, L, u_skyUp) * u_sunColor.rgb * visibility * u_eclipseParams.x;
	// Underwater: the sun crossed the wavy surface — caustic focus + Beer-Lambert absorption
	// (underwater_light.inc.glsl), so seabed/submerged objects get the dancing light patterns. One
	// water-level fetch + a branch above water; only underwater pixels pay for the wave taps.
	if (terrainHeightMapPresent())
	{
		const float depthBelow = terrainDataAt(worldPos.xz).y - worldPos.y;
		if (depthBelow > 0.0)
			lightRadiance *= underwaterSunTransmittance(worldPos.xz, depthBelow, 0.0, 1.0); // surfaces: physical reach
	}
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
		if (d <= 0.0) // background (reversed-Z far = 0)
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
// terrain_height.inc.glsl is included unconditionally above (underwater caustics use it too).

// The baked terrain fields at one point (fog terrain cascades: height, water level, MACRO ALTITUDE +
// temperature/humidity), with mild-climate fallbacks when no map is bound. The altitude/height split is
// what tells a MOUNTAIN (height far above the local macro altitude -> crag rock) from high-altitude
// flatland (grass at elevation); the generator's temperature lapse already cools high ground.
struct TerrainFields
{
	float altitude;    // macro altitude (m above sea level)
	float temperature; // Celsius
	float humidity;    // [0,1]
	float waterLevel;  // world Y of the local water surface
};

TerrainFields terrainFields(vec3 worldPos)
{
	const float seaLevel = u_terrainFade.z; // the streamer's live sea level rides the edge-fade params
	TerrainFields f;
	f.altitude = worldPos.y - seaLevel;
	f.temperature = 12.5;
	f.humidity = 0.5;
	f.waterLevel = seaLevel;
	if (terrainHeightMapPresent())
	{
		const vec4 td = terrainDataAt(worldPos.xz);
		f.altitude = td.w;
		f.waterLevel = td.y;
		const vec4 climate = terrainClimateAt(worldPos.xz); // (fog thickness, falloff mul, temp C, humidity)
		f.temperature = climate.z;
		f.humidity = climate.w;
	}
	return f;
}

// Procedural flat-color terrain albedo: the far-distance / no-texture-set fallback (and the GI-facing
// approximation of the splatted result).
vec3 terrainFlatAlbedo(vec3 worldPos, vec3 N, TerrainFields f)
{
	const vec3 sand   = vec3(0.76, 0.70, 0.50);
	const vec3 steppe = vec3(0.55, 0.52, 0.33);
	const vec3 grass  = vec3(0.26, 0.42, 0.17);
	const vec3 jungle = vec3(0.10, 0.30, 0.12);
	const vec3 rock   = vec3(0.37, 0.34, 0.31);
	const vec3 snow   = vec3(0.90, 0.93, 0.97);

	const float seaLevel = u_terrainFade.z;
	const float h = worldPos.y - seaLevel;
	const float slope = 1.0 - clamp(N.y, 0.0, 1.0);
	// Cheap value noise to break up band edges.
	const float n = fract(sin(dot(floor(worldPos.xz * 0.08), vec2(12.9898, 78.233))) * 43758.5453) * 2.0 - 1.0;

	// Ground cover by climate: dry -> sand/steppe, mild -> grass, saturated -> deep jungle green.
	vec3 col = mix(sand, steppe, smoothstep(0.08, 0.25, f.humidity + n * 0.02));
	col = mix(col, grass, smoothstep(0.25, 0.50, f.humidity + n * 0.02));
	col = mix(col, jungle, smoothstep(0.65, 0.95, f.humidity));

	// Beach band just above the local waterline (sea or a river/lake surface at altitude).
	const float aboveWater = worldPos.y - f.waterLevel;
	col = mix(sand, col, smoothstep(0.4, 3.0 + n, aboveWater));

	// Mountain-ness: local relief ABOVE the macro altitude — a ridge poking out of a plateau reads as
	// rock while a high-altitude grassland stays grass. Steep faces are bare rock regardless.
	const float crag = smoothstep(10.0, 45.0, h - f.altitude);
	col = mix(col, rock, max(crag * 0.85, smoothstep(0.45, 0.70, slope + n * 0.03)));

	// Snow where it's COLD (the generator's altitude lapse + climate bands decide, not raw height);
	// steep faces shed it. Temperature is in Celsius: snow fades in below ~-8C, full by ~-19C.
	const float snowAmt = smoothstep(-8.5, -19.0, f.temperature) * (1.0 - smoothstep(0.5, 0.75, slope));
	col = mix(col, snow, snowAmt);
	return col;
}

// --- Biome texture splatting (setTerrainBiomeMaterials; u_terrainTexParams*/u_terrainBiomeCoords) ------
// Ground biomes are climate attractors in the generator's (t01, h01) space: per-pixel Gaussian weights
// select the top two, their textures blend, and a rock layer (its own climate-picked attractor set)
// takes over on steep slopes and on ridges rising far above the macro altitude. World-XZ projection for
// ground, triplanar for rock (XZ stretches to smear on cliff faces).

struct TerrainSample
{
	vec3 albedo;
	vec3 normal;   // world space
	float rough;
	float metal;
	float ao;
};

// One splat material sampled with world-XZ UVs; tangent basis is the world X/Z axes reoriented onto the
// geometric normal. matIdx diverges between neighbouring pixels at biome borders -> nonuniformEXT.
TerrainSample sampleTerrainXZ(uint matIdx, vec2 uv, vec3 geoN)
{
	const MaterialInfo material = in_materialInfos[nonuniformEXT(matIdx)];
	const uint diffuseTexIdx = material.diffuseNormalTexIdx & 0xFFFFu;
	const uint normalTexIdx  = material.diffuseNormalTexIdx >> 16;
	const uint armTexIdx     = material.metalRoughnessTexIdxAlphaMode & 0xFFFFu;

	TerrainSample s;
	s.albedo = texture(u_textures[nonuniformEXT(diffuseTexIdx)], uv).rgb;
	const vec3 arm = armTexIdx != 0xFFFFu ? texture(u_textures[nonuniformEXT(armTexIdx)], uv).rgb : vec3(1.0, 0.9, 0.0);
	s.ao = arm.r;
	s.rough = max(arm.g, 0.01);
	s.metal = arm.b;

	const vec3 normalSample = texture(u_textures[nonuniformEXT(normalTexIdx)], uv).xyz;
	vec3 tn;
	if ((material.flags & MATERIAL_FLAG_BC5_NORMAL) != 0u)
	{
		const vec2 nxy = normalSample.xy * 2.0 - 1.0;
		tn = vec3(nxy, sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
	}
	else
		tn = normalize(normalSample * 2.0 - 1.0);
	// uv = worldPos.xz * scale: tangent = world +X, bitangent = world +Z, reoriented onto the surface.
	s.normal = normalize(tn.x * vec3(1.0, 0.0, 0.0) + tn.y * vec3(0.0, 0.0, 1.0) + tn.z * geoN);
	return s;
}

// Triplanar version for the rock layer: three world-axis projections blended by the normal's axis
// alignment, each plane's tangent normal reoriented per-plane (whiteout-style).
TerrainSample sampleTerrainTriplanar(uint matIdx, vec3 worldPos, vec3 geoN, float uvScale)
{
	const MaterialInfo material = in_materialInfos[nonuniformEXT(matIdx)];
	const uint diffuseTexIdx = material.diffuseNormalTexIdx & 0xFFFFu;
	const uint normalTexIdx  = material.diffuseNormalTexIdx >> 16;
	const uint armTexIdx     = material.metalRoughnessTexIdxAlphaMode & 0xFFFFu;
	const bool bc5 = (material.flags & MATERIAL_FLAG_BC5_NORMAL) != 0u;

	vec3 w = abs(geoN);
	w = w / (w.x + w.y + w.z);
	const vec2 uvX = worldPos.zy * uvScale; // plane normal = X
	const vec2 uvY = worldPos.xz * uvScale; // plane normal = Y
	const vec2 uvZ = worldPos.xy * uvScale; // plane normal = Z

	TerrainSample s;
	s.albedo = texture(u_textures[nonuniformEXT(diffuseTexIdx)], uvX).rgb * w.x
	         + texture(u_textures[nonuniformEXT(diffuseTexIdx)], uvY).rgb * w.y
	         + texture(u_textures[nonuniformEXT(diffuseTexIdx)], uvZ).rgb * w.z;
	vec3 arm = vec3(1.0, 0.9, 0.0);
	if (armTexIdx != 0xFFFFu)
		arm = texture(u_textures[nonuniformEXT(armTexIdx)], uvX).rgb * w.x
		    + texture(u_textures[nonuniformEXT(armTexIdx)], uvY).rgb * w.y
		    + texture(u_textures[nonuniformEXT(armTexIdx)], uvZ).rgb * w.z;
	s.ao = arm.r;
	s.rough = max(arm.g, 0.01);
	s.metal = arm.b;

	const vec3 nsX = texture(u_textures[nonuniformEXT(normalTexIdx)], uvX).xyz;
	const vec3 nsY = texture(u_textures[nonuniformEXT(normalTexIdx)], uvY).xyz;
	const vec3 nsZ = texture(u_textures[nonuniformEXT(normalTexIdx)], uvZ).xyz;
	vec3 tnX, tnY, tnZ;
	if (bc5)
	{
		const vec2 xy0 = nsX.xy * 2.0 - 1.0, xy1 = nsY.xy * 2.0 - 1.0, xy2 = nsZ.xy * 2.0 - 1.0;
		tnX = vec3(xy0, sqrt(max(1.0 - dot(xy0, xy0), 0.0)));
		tnY = vec3(xy1, sqrt(max(1.0 - dot(xy1, xy1), 0.0)));
		tnZ = vec3(xy2, sqrt(max(1.0 - dot(xy2, xy2), 0.0)));
	}
	else
	{
		tnX = normalize(nsX * 2.0 - 1.0);
		tnY = normalize(nsY * 2.0 - 1.0);
		tnZ = normalize(nsZ * 2.0 - 1.0);
	}
	// Whiteout blend: each plane's XY detail swizzled into its world plane, Z along the plane normal.
	const vec3 nX = vec3(tnX.z * sign(geoN.x), tnX.y, tnX.x);
	const vec3 nY = vec3(tnY.x, tnY.z * sign(geoN.y), tnY.y);
	const vec3 nZ = vec3(tnZ.x, tnZ.y, tnZ.z * sign(geoN.z));
	s.normal = normalize(nX * w.x + nY * w.y + nZ * w.z + geoN * 2.0); // biased toward geoN: detail, not replacement
	return s;
}

void terrainWeighted(inout TerrainSample acc, TerrainSample s, float w)
{
	acc.albedo += s.albedo * w;
	acc.normal += s.normal * w;
	acc.rough  += s.rough * w;
	acc.metal  += s.metal * w;
	acc.ao     += s.ao * w;
}

// The full splatted terrain surface. Returns false when no texture set is bound or the pixel is past the
// fade-out distance (caller falls back to the flat-color path).
bool terrainSplat(vec3 worldPos, vec3 geoN, TerrainFields f, float distFade, out TerrainSample surf)
{
	const int baseMat = int(u_terrainTexParams0.x);
	const int numGround = int(u_terrainTexParams0.y);
	const int numRock = int(u_terrainTexParams0.z);
	if (baseMat < 0 || numGround <= 0 || distFade >= 1.0)
		return false;

	// Climate position of this pixel in the generator's (t01, h01) attractor space. The baked
	// temperature includes the altitude lapse, so cold peaks drift toward the Tundra/Arctic attractors
	// (rocky/snowy textures) exactly where the generator grows them.
	const vec2 climate = vec2(clamp((f.temperature + 25.0) / 75.0, 0.0, 1.0), f.humidity);
	const float invS2 = 1.0 / (2.0 * u_terrainTexParams0.w * u_terrainTexParams0.w);

	// Top-3 ground biomes by Gaussian proximity. Only the top two get sampled, but their weights are
	// taken RELATIVE to the third (w - w2): when the #2/#3 ranking swaps, both candidates sit exactly at
	// w2 and enter/leave the blend at zero contribution — without this, the second texture flips
	// instantly mid-blend and every ranking crossover shows as a hard seam.
	int i0 = 0, i1 = 0;
	float w0 = -1.0, w1 = -1.0, w2 = -1.0;
	for (int i = 0; i < numGround; ++i)
	{
		const vec2 d = climate - u_terrainBiomeCoords[i].xy;
		const float w = exp(-dot(d, d) * invS2);
		if (w > w0)      { i1 = i0; w2 = w1; w1 = w0; i0 = i; w0 = w; }
		else if (w > w1) { i1 = i; w2 = w1; w1 = w; }
		else if (w > w2) { w2 = w; }
	}
	w2 = max(w2, 0.0); // numGround < 3: nothing to subtract
	const float w0r = w0 - w2;
	const float w1r = max(w1 - w2, 0.0);
	// Weights can all underflow far from every attractor; the max() keeps the blend defined (top-1 wins).
	const float wSum = max(w0r + w1r, 1e-6);
	float blend1 = w1r / wSum;

	const vec2 uvGround = worldPos.xz * u_terrainTexParams1.x;
	TerrainSample surfAcc = TerrainSample(vec3(0.0), vec3(0.0), 0.0, 0.0, 0.0);
	terrainWeighted(surfAcc, sampleTerrainXZ(uint(baseMat + i0), uvGround, geoN), 1.0 - blend1);
	if (blend1 > 0.004)
		terrainWeighted(surfAcc, sampleTerrainXZ(uint(baseMat + i1), uvGround, geoN), blend1);

	// Beach sand just above the local waterline: blend toward the dedicated beach entry (NOT a biome —
	// a climate-independent shoreline overlay, always the LAST registered material when present).
	if (u_terrainTexParams3.x > 0.5)
	{
		const float beachW = 1.0 - smoothstep(0.3, max(u_terrainTexParams2.z, 0.31), worldPos.y - f.waterLevel);
		if (beachW > 0.004)
		{
			const uint beachMatIdx = uint(baseMat) + uint(numGround) + uint(numRock);
			const TerrainSample sand = sampleTerrainXZ(beachMatIdx, uvGround, geoN);
			surfAcc.albedo = mix(surfAcc.albedo, sand.albedo, beachW);
			surfAcc.normal = mix(surfAcc.normal, sand.normal, beachW);
			surfAcc.rough  = mix(surfAcc.rough, sand.rough, beachW);
			surfAcc.metal  = mix(surfAcc.metal, sand.metal, beachW);
			surfAcc.ao     = mix(surfAcc.ao, sand.ao, beachW);
		}
	}

	// Rock layer: steep slopes + crags (relief above the macro altitude). Rock type is the nearest rock
	// attractor in the same climate space (gray granite cold, red sandstone hot/dry, dark basalt wet).
	const float slope = 1.0 - clamp(geoN.y, 0.0, 1.0);
	const float seaLevel = u_terrainFade.z;
	const float crag = smoothstep(u_terrainTexParams2.x, u_terrainTexParams2.y, (worldPos.y - seaLevel) - f.altitude);
	float rockW = max(smoothstep(u_terrainTexParams1.z, u_terrainTexParams1.w, slope), crag * 0.85);
	if (rockW > 0.004 && numRock > 0)
	{
		// Top-2 rock types with the same third-weight subtraction as the ground blend (a top-1 pick
		// flips the whole cliff texture at the ranking crossover).
		int r0 = 0, r1 = 0;
		float rw0 = -1.0, rw1 = -1.0, rw2 = -1.0;
		for (int i = 0; i < numRock; ++i)
		{
			const vec2 d = climate - u_terrainBiomeCoords[numGround + i].xy;
			const float w = exp(-dot(d, d) * invS2);
			if (w > rw0)      { r1 = r0; rw2 = rw1; rw1 = rw0; r0 = i; rw0 = w; }
			else if (w > rw1) { r1 = i; rw2 = rw1; rw1 = w; }
			else if (w > rw2) { rw2 = w; }
		}
		rw2 = max(rw2, 0.0);
		const float rw1r = max(rw1 - rw2, 0.0);
		const float rockBlend1 = rw1r / max((rw0 - rw2) + rw1r, 1e-6);

		TerrainSample rock = sampleTerrainTriplanar(uint(baseMat + numGround + r0), worldPos, geoN, u_terrainTexParams1.y);
		if (rockBlend1 > 0.004)
		{
			const TerrainSample rockB = sampleTerrainTriplanar(uint(baseMat + numGround + r1), worldPos, geoN, u_terrainTexParams1.y);
			rock.albedo = mix(rock.albedo, rockB.albedo, rockBlend1);
			rock.normal = normalize(mix(rock.normal, rockB.normal, rockBlend1));
			rock.rough  = mix(rock.rough, rockB.rough, rockBlend1);
			rock.metal  = mix(rock.metal, rockB.metal, rockBlend1);
			rock.ao     = mix(rock.ao, rockB.ao, rockBlend1);
		}
		surfAcc.albedo = mix(surfAcc.albedo, rock.albedo, rockW);
		surfAcc.normal = mix(surfAcc.normal, rock.normal, rockW);
		surfAcc.rough  = mix(surfAcc.rough, rock.rough, rockW);
		surfAcc.metal  = mix(surfAcc.metal, rock.metal, rockW);
		surfAcc.ao     = mix(surfAcc.ao, rock.ao, rockW);
	}

	surfAcc.normal = normalize(surfAcc.normal);
	// Far fade back to the flat colors + geometric normal (hides tiling, spares texture cache).
	if (distFade > 0.0)
	{
		surfAcc.albedo = mix(surfAcc.albedo, terrainFlatAlbedo(worldPos, geoN, f), distFade);
		surfAcc.normal = normalize(mix(surfAcc.normal, geoN, distFade));
		surfAcc.rough  = mix(surfAcc.rough, 0.92, distFade);
		surfAcc.metal  = mix(surfAcc.metal, 0.0, distFade);
		surfAcc.ao     = mix(surfAcc.ao, 1.0, distFade);
	}
	surf = surfAcc;
	return true;
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
	const vec3 geoN = normalize(in_tbn[2]); // geometric (interpolated vertex) normal
	const TerrainFields fields = terrainFields(in_pos);
	// Texture fade-out band: [end/2 .. end] of camera distance blends back to the flat colors.
	const float fadeEnd = u_terrainTexParams2.w;
	const float distFade = fadeEnd > 0.0 ? smoothstep(fadeEnd * 0.5, fadeEnd, distance(u_viewPos, in_pos)) : 0.0;
	float terrainTexAO = 1.0;
	TerrainSample surf;
	if (terrainSplat(in_pos, geoN, fields, distFade, surf))
	{
		materialColor = surf.albedo;
		N = surf.normal;
		roughness = surf.rough;
		metalness = surf.metal;
		terrainTexAO = surf.ao;
	}
	else
	{
		N = geoN;
		materialColor = terrainFlatAlbedo(in_pos, geoN, fields);
		roughness = 0.92;
		metalness = 0.0;
	}
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
	// Blend to the virtual sky probe over the probe field's outer band (coverage) instead of stepping
	// at the outermost cascade's window face; the fallback is only evaluated where it contributes.
	float giCoverage;
	const vec3 indirectE = evalProbeSHCoverage(in_pos, bentN, giCoverage);
	vec3 indirect = (indirectE.x >= 0.0) ? (indirectE / PI) : vec3(0.0);
	if (giCoverage < 1.0)
		indirect = mix(giEvalSkySH(bentN) / PI, indirect, giCoverage);
	indirect *= u_aoParams.y;
#ifdef TERRAIN
	ao *= terrainTexAO; // baked texture AO on top of the screen-space term
#endif
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