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
		const float localWaterLevel = terrainDataAt(worldPos.xz).y;
		float depthBelow = localWaterLevel - worldPos.y;
		// Swash band: gate against the LIVE displaced surface, not the calm level — a receded wave
		// leaves sand below the calm line dry (no caustics/absorption tint on exposed bottom), and the
		// run-up tongue is lit as underwater while it covers the beach. Points deeper than the swash
		// reach are underwater at any wave phase and skip the wave taps (u_oceanParams7.w is 0 with
		// swash off, so this is free for non-swash setups).
		const float swashReach = u_oceanParams7.w;
		if (swashReach > 0.0 && abs(depthBelow) < swashReach)
			depthBelow += underwaterLiveWaveY(worldPos.xz, depthBelow, localWaterLevel);
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
	const float seaLevel = u_terrainParams.z; // the streamer's live sea level
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
		const vec4 climate = terrainClimateAt(worldPos.xz); // (fog thickness, lapse rate, temp C, humidity)
		f.humidity = climate.w;

		// Re-derive the temperature at THIS PIXEL'S height. The baked temperature is only valid at the
		// baked height (td.x), and the two cascades bake different heights for the same spot: the near one
		// the full-detail surface, the far one its 7.68 km average — which cannot know a peak exists, so it
		// reports the temperature of the plateau the peak stands on. Left uncorrected the far cascade omits
		// peak cold entirely and summits read up to 10.6 C too warm, flipping their texture as the camera
		// crosses the cascade crossfade.
		// Moving it to worldPos.y fixes both levels with one expression, and it is self-cancelling rather
		// than cascade-aware: near bakes td.x ~= worldPos.y so the correction vanishes on its own, while far
		// supplies exactly the missing lapse over the relief. Clamped at sea level to match how the
		// generator applies the lapse in the first place (only over land).
		// climate.y is per unit of the GENERATOR's vertical frame, so the world-metre delta converts through
		// vertScale. Baked in that frame on purpose: the world-frame rate runs to -0.065 on a compressed
		// world and would clamp to nothing in 8 bits.
		const float bakedElev = max(td.x - seaLevel, 0.0);
		const float pixelElev = max(worldPos.y - seaLevel, 0.0);
		f.temperature = climate.z + climate.y * (pixelElev - bakedElev) / u_terrainParams.y;
	}
	return f;
}

// --- Terrain field debug view -------------------------------------------------------------------------
// Set a non-zero mode and hit F5 (shaders compile at runtime) to draw the baked terrain data map instead
// of shading it. The bake is otherwise invisible from here: a wrong sampler, a wrong pack and a wrong
// upload all look identical on-screen, and "the texture is wrong" cannot tell them apart.
//   1 = temperature    blue(-25 C) -> cyan -> green -> yellow -> red(+50 C), with 5 C contour lines, plus:
//                      MAGENTA line = the FREEZING LINE (0 C)
//                      CYAN line + darkened fill = the snow line (the live Terrain/Textures "Snow temp
//                        none" tweak — everything colder than it holds some snow). If that region is
//                        empty, snow cannot happen anywhere in view.
//                      A CONSTANT field draws no contour lines at all — that alone tells you the field is
//                      stuck rather than merely uniform-looking.
//   2 = humidity       black (arid) -> white (h01 = 1), contour lines every 0.1
//   3 = crag relief    |height - altitude|: black 0 m -> white 200 m. This drives the rock layer; if it
//                      is black everywhere, mountains keep their lowland ground texture.
//   4 = altitude       the macro band: black = sea level -> white = 5 km
//   5 = cascade        green = near cascade, red = far, yellow = the crossfade band between them
#define TERRAIN_DEBUG_MODE 0

#if TERRAIN_DEBUG_MODE != 0
vec3 debugHeatRamp(float t)
{
	t = clamp(t, 0.0, 1.0);
	const float s = t * 4.0;
	if (s < 1.0) return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 1.0), s);
	if (s < 2.0) return mix(vec3(0.0, 1.0, 1.0), vec3(0.0, 1.0, 0.0), s - 1.0);
	if (s < 3.0) return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), s - 2.0);
	return mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), s - 3.0);
}

// A dark line wherever `v` crosses a multiple of `spacing` — makes a smooth ramp readable as values
// rather than a vague wash, and makes a CONSTANT field obvious (no lines at all).
float debugContour(float v, float spacing)
{
	const float f = abs(fract(v / spacing - 0.5) - 0.5) / fwidth(v / spacing);
	return 1.0 - clamp(1.0 - f, 0.0, 1.0) * 0.7;
}

// Coverage of a single isoline at `v == level`, `widthPx` pixels wide. Dividing by fwidth is what keeps
// the line a constant width ON SCREEN however fast the field varies — a fixed |v - level| < eps test
// instead paints a fat band across a plain and vanishes entirely on a steep face, which is precisely
// where an isotherm is most interesting.
float debugIsoline(float v, float level, float widthPx)
{
	const float d = abs(v - level) / max(fwidth(v), 1e-6);
	return 1.0 - smoothstep(0.0, widthPx, d);
}

vec3 terrainDebugColor(TerrainFields f, vec3 worldPos)
{
#if TERRAIN_DEBUG_MODE == 1
	vec3 col = debugHeatRamp((f.temperature + 25.0) / 75.0) * debugContour(f.temperature, 5.0);
	// The snow zone, read from the LIVE snow params rather than a mirrored constant, so this marks where
	// the splat actually lays snow down instead of where it did when this line was last hand-edited.
	const float snowLineC = u_terrainTexParams3.w;
	if (f.temperature <= snowLineC)
		col = mix(col, vec3(0.05), 0.75);
	// The FREEZING LINE (0 C) in magenta, and the snow line in cyan. Drawn last and thick so they read
	// over the ramp: these two are the boundaries that actually decide what the terrain looks like.
	col = mix(col, vec3(0.0, 1.0, 1.0), debugIsoline(f.temperature, snowLineC, 1.5));
	col = mix(col, vec3(1.0, 0.0, 1.0), debugIsoline(f.temperature, 0.0, 2.0));
	return col;
#elif TERRAIN_DEBUG_MODE == 2
	return vec3(clamp(f.humidity, 0.0, 1.0)) * debugContour(f.humidity, 0.1);
#elif TERRAIN_DEBUG_MODE == 3
	const float relief = abs((worldPos.y - u_terrainParams.z) - f.altitude);
	return debugHeatRamp(relief / 200.0) * debugContour(relief, 25.0);
#elif TERRAIN_DEBUG_MODE == 4
	return vec3(clamp(f.altitude / 5000.0, 0.0, 1.0)) * debugContour(f.altitude, 250.0);
#else // 5 = which cascade fed this pixel
	const vec2 uv0 = (worldPos.xz - u_fogParams5.xy) * u_fogParams3.y + 0.5;
	const float edge = max(abs(uv0.x - 0.5), abs(uv0.y - 0.5));
	const float nearW = u_fogParams5.z > 0.0 ? 1.0 - smoothstep(0.42, 0.48, edge) : 1.0;
	return mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), nearW);
#endif
}
#endif

// --- Terrain texture splatting (setTerrainSplatMaterials; u_terrainTexParams*/u_terrainSplatClimate) ---
// The surface is composited as four physical layers, bottom-up, in the order nature stacks them:
//   1. GROUND — the soil/vegetation the climate grows; selected by the pixel's (temperature, humidity)
//               against each entry's climate BOX. World-XZ projection.
//   2. BEACH  — a shoreline band just above the local waterline. Not climate-selected: sand is what a
//               wave leaves behind whatever the weather is doing.
//   3. ROCK   — bedrock, wherever the surface is too steep or too prominent to hold soil. Its type IS
//               climate-selected (weathering depends on climate). Triplanar (XZ smears on cliff faces).
//   4. SNOW   — cover over ALL of the above. This is the layer that makes cold mountains read correctly,
//               and it is why snow is not a ground type: snow falls ON the bedrock, so a ground-level
//               snow entry would just be painted over by layer 3 and the peaks would come out gray.
// There is no "biome" here — no enum, no discrete regions. Climate selects textures directly.

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

// --- Crag wander noise ------------------------------------------------------------------------------
// Value-noise fBm over world XZ. Lives here rather than in the generator because the wander has to be
// scaled by the LOCAL RELIEF, and relief is the one thing the generator's coarse path cannot supply
// (macro == elev there, so its relief is 0 by construction and the far cascade would get no wander while
// the near one did — the cascades would disagree again). The shader always has the full-detail mesh
// height, so it can always form the real relief.
float terrainHash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float terrainValueNoise(vec2 p)
{
	const vec2 i = floor(p);
	const vec2 f = p - i;
	const vec2 u = f * f * (3.0 - 2.0 * f);
	return mix(mix(terrainHash12(i),                terrainHash12(i + vec2(1.0, 0.0)), u.x),
	           mix(terrainHash12(i + vec2(0.0, 1.0)), terrainHash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

// ~[-1, 1], 3 octaves. Amplitude-normalised, so octaves add roughness rather than range.
float terrainFbm(vec2 p)
{
	float v = 0.0, a = 0.5, norm = 0.0;
	for (int i = 0; i < 3; ++i)
	{
		v += a * terrainValueNoise(p);
		norm += a;
		p *= 2.0;
		a *= 0.5;
	}
	return (v / norm) * 2.0 - 1.0;
}

// Lay `s` over `acc` with coverage `w`.
void terrainMixInto(inout TerrainSample acc, TerrainSample s, float w)
{
	acc.albedo = mix(acc.albedo, s.albedo, w);
	acc.normal = mix(acc.normal, s.normal, w);
	acc.rough  = mix(acc.rough, s.rough, w);
	acc.metal  = mix(acc.metal, s.metal, w);
	acc.ao     = mix(acc.ao, s.ao, w);
}

// How well `climate` matches an entry's climate BOX: 1 anywhere inside it, Gaussian-decaying by the
// distance to its edge outside. A box rather than a point is what lets an entry opt OUT of an axis it
// does not care about — leaving a range at full 0..1 width contributes no distance on it — so "bedrock
// that is cold at ANY humidity" is directly expressible. With point attractors it was not, and every
// entry had to claim some fictional humidity to sit at.
float climateBoxWeight(vec2 climate, vec4 box, float invS2)
{
	const vec2 d = max(max(box.xz - climate, climate - box.yw), vec2(0.0));
	return exp(-dot(d, d) * invS2);
}

struct ClimatePick
{
	int i0;        // best entry, as an ENTRY index (0-based, like u_terrainSplatClimate) — the caller adds
	int i1;        // runner-up                       baseMat to reach the material
	float blend1;  // coverage of i1 over i0, in [0, 1]
};

// The top two climate entries in [first, first + count), with weights taken RELATIVE to the third
// (w - w2): when the #2/#3 ranking swaps, both candidates sit exactly at w2 and so enter and leave the
// blend at zero contribution. Without this the second texture flips instantly mid-blend and every
// ranking crossover draws a hard seam across the terrain.
ClimatePick pickClimate(vec2 climate, int first, int count, float invS2)
{
	int i0 = first, i1 = first;
	float w0 = -1.0, w1 = -1.0, w2 = -1.0;
	for (int i = first; i < first + count; ++i)
	{
		const float w = climateBoxWeight(climate, u_terrainSplatClimate[i], invS2);
		if (w > w0)      { i1 = i0; w2 = w1; w1 = w0; i0 = i; w0 = w; }
		else if (w > w1) { i1 = i; w2 = w1; w1 = w; }
		else if (w > w2) { w2 = w; }
	}
	w2 = max(w2, 0.0); // count < 3: nothing to subtract
	const float w1r = max(w1 - w2, 0.0);
	// Every weight can underflow when the climate sits far outside all boxes; the max() keeps the blend
	// defined (top-1 wins outright).
	return ClimatePick(i0, i1, w1r / max((w0 - w2) + w1r, 1e-6));
}

// The full splatted terrain surface. When no texture set is bound yet (early startup, before the DDS bake
// registers) it returns a neutral mid-gray so the material reads stay in bounds.
TerrainSample terrainSplat(vec3 worldPos, vec3 geoN, TerrainFields f)
{
	const int baseMat = int(u_terrainTexParams0.x);
	const int numGround = int(u_terrainTexParams0.y);
	const int numRock = int(u_terrainTexParams0.z);
	if (baseMat < 0 || numGround <= 0)
		return TerrainSample(vec3(0.5), geoN, 0.92, 0.0, 1.0);

	// This pixel's climate, in the same normalized space the table's boxes are written in. The baked
	// temperature already carries the generator's altitude lapse, so elevation enters the selection as the
	// cold it actually causes rather than as a raw height threshold — one field decides both where snow
	// lies and which vegetation grows under it, and they cannot disagree.
	const vec2 climate = vec2(clamp((f.temperature + 25.0) / 75.0, 0.0, 1.0), f.humidity);
	const float invS2 = 1.0 / (2.0 * u_terrainTexParams0.w * u_terrainTexParams0.w);
	const float slope = 1.0 - clamp(geoN.y, 0.0, 1.0);
	const vec2 uvGround = worldPos.xz * u_terrainTexParams1.x;

	// --- 1. Ground: whatever the climate grows here.
	const ClimatePick g = pickClimate(climate, 0, numGround, invS2);
	TerrainSample surf = sampleTerrainXZ(uint(baseMat + g.i0), uvGround, geoN);
	if (g.blend1 > 0.004)
		terrainMixInto(surf, sampleTerrainXZ(uint(baseMat + g.i1), uvGround, geoN), g.blend1);

	// --- 2. Beach: the band just above the local waterline, whatever the climate.
	if (u_terrainTexParams3.x > 0.5)
	{
		const float beachW = 1.0 - smoothstep(0.3, max(u_terrainTexParams2.z, 0.31), worldPos.y - f.waterLevel);
		if (beachW > 0.004)
			terrainMixInto(surf, sampleTerrainXZ(uint(baseMat + numGround + numRock), uvGround, geoN), beachW);
	}

	// --- 3. Bedrock, exposed where the surface cannot hold soil: too steep, or standing too far above the
	// macro altitude (a crag). max(), not a sum — the two reasons coincide on a cliff and adding them
	// would double-count. These are NOT redundant: at the model's 30 m/px the field rarely reaches the
	// slope threshold, so on V3 terrain crag is what puts rock on nearly every mountain.
	//
	// The relief is WANDERED before the test. Without it the rock boundary is an elevation contour: V3's
	// macro altitude is the coarse stage's 7.68 km surface, which is nearly flat across any one mountain,
	// so relief ~= height - constant and grass gives way to stone at the same height right across a range.
	// The wander is scaled by the relief itself, which is what stops it inventing rock: |wander| <= relief
	// keeps the tested value in [0, 2*relief], so flat lowlands (relief ~ 0) are untouched no matter how
	// large the amplitude, and only ground that already stands proud gets moved.
	float relief = (worldPos.y - u_terrainParams.z) - f.altitude;
	if (u_terrainTexParams5.x > 0.0)
	{
		const float w = terrainFbm(worldPos.xz * u_terrainTexParams5.y) * u_terrainTexParams5.x;
		relief -= w * clamp(relief / u_terrainTexParams5.x, 0.0, 1.0);
	}
	const float crag = smoothstep(u_terrainTexParams2.x, u_terrainTexParams2.y, relief);
	const float rockW = max(smoothstep(u_terrainTexParams1.z, u_terrainTexParams1.w, slope), crag * 0.85);
	if (rockW > 0.004 && numRock > 0)
	{
		const ClimatePick r = pickClimate(climate, numGround, numRock, invS2);
		TerrainSample rock = sampleTerrainTriplanar(uint(baseMat + r.i0), worldPos, geoN, u_terrainTexParams1.y);
		if (r.blend1 > 0.004)
			terrainMixInto(rock, sampleTerrainTriplanar(uint(baseMat + r.i1), worldPos, geoN, u_terrainTexParams1.y), r.blend1);
		terrainMixInto(surf, rock, rockW);
	}

	// --- 4. Snow, over ALL of the above. Snow settles on bedrock exactly as it settles on soil, so it has
	// to be applied after the rock layer rather than selected as a cold ground type — that ordering is the
	// difference between a white peak and a gray one. What keeps it from being a flat white cap is that it
	// slides off anything steep, so the mountain's own rock shows through on its faces.
	if (u_terrainTexParams3.y > 0.5)
	{
		const float cold  = 1.0 - smoothstep(u_terrainTexParams3.z, u_terrainTexParams3.w, f.temperature);
		const float holds = 1.0 - smoothstep(u_terrainTexParams4.x, u_terrainTexParams4.y, slope);
		// Nothing accumulates where nothing falls: without this the cold DRY ground (polar desert, and the
		// arid side of any high range) turns white too.
		const float wet = smoothstep(0.0, max(u_terrainTexParams4.z, 1e-3), f.humidity);
		const float snowW = cold * holds * wet;
		if (snowW > 0.004)
		{
			const uint snowMatIdx = uint(baseMat + numGround + numRock) + (u_terrainTexParams3.x > 0.5 ? 1u : 0u);
			terrainMixInto(surf, sampleTerrainXZ(snowMatIdx, worldPos.xz * u_terrainTexParams2.w, geoN), snowW);
		}
	}

	surf.normal = normalize(surf.normal);
	return surf;
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
	const TerrainSample surf = terrainSplat(in_pos, geoN, fields);
	materialColor = surf.albedo;
	N = surf.normal;
	roughness = surf.rough;
	metalness = surf.metal;
	const float terrainTexAO = surf.ao;
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
#if TERRAIN_DEBUG_MODE != 0
	out_color = vec4(terrainDebugColor(fields, in_pos), 1.0); // unlit: the field itself, not its shading
	return;
#endif
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