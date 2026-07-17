#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_query : enable

// Procedural terrain variant of instanced_indirect.fs.glsl (EPipelineIndex::TerrainLit): same lighting
// core (instanced_indirect_lit.inc.glsl), but the material albedo is replaced by climate-picked texture
// splatting — procedural terrain chunks carry no textures of their own.

#include "shared.inc.glsl"

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv; // unused (shared VS interface)
layout (location = 5) in flat uint in_meshIdxMaterialIdx;
#ifdef STEREO
layout (push_constant) uniform ViewPC { uint u_viewIndex; };
#endif

layout (location = 0) out vec4 out_color;

#include "instanced_indirect_lit.inc.glsl"

// Baked terrain fields at one point (terrain-data cascades), mild-climate fallbacks without a map.
// altitude is the MACRO band: height far above it = mountain crag; height ~ altitude = flatland.
struct TerrainFields
{
	float altitude;    // macro altitude (m above sea level)
	float temperature; // Celsius
	float humidity;    // [0,1]
	float waterLevel;  // world Y of the local water surface
};

TerrainFields terrainFields(vec3 worldPos)
{
	const float seaLevel = u_terrainParams.z;
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
		const vec4 climate = terrainClimateAt(worldPos.xz);
		f.humidity = climate.w;
		// The map stores a SEA-LEVEL baseline + one lapse rate, evaluated at the shaded height — never
		// bake a temperature sample (only valid at the height it was taken; the cascades' heights differ).
		f.temperature = terrainTemperatureAt(climate, worldPos.y);
	}
	return f;
}

// --- Terrain field debug view: set a mode, F5. Draws the baked data map instead of shading it. ---
//   1 = temperature  heat ramp -25..+50 C, 5 C contours; MAGENTA = freezing line, CYAN + darkened
//                    fill = the live snow line. No contours at all = the field is stuck.
//   2 = humidity     black -> white, 0.1 contours
//   3 = crag relief  |height - altitude|: black 0 -> white 200 m (drives the rock layer)
//   4 = altitude     black sea level -> white 5 km
//   5 = cascade      green = near, red = far, yellow = crossfade band
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

// Dark line at every multiple of `spacing`.
float debugContour(float v, float spacing)
{
	const float f = abs(fract(v / spacing - 0.5) - 0.5) / fwidth(v / spacing);
	return 1.0 - clamp(1.0 - f, 0.0, 1.0) * 0.7;
}

// Single isoline at `v == level`; /fwidth keeps it a constant width ON SCREEN regardless of gradient.
float debugIsoline(float v, float level, float widthPx)
{
	const float d = abs(v - level) / max(fwidth(v), 1e-6);
	return 1.0 - smoothstep(0.0, widthPx, d);
}

vec3 terrainDebugColor(TerrainFields f, vec3 worldPos)
{
#if TERRAIN_DEBUG_MODE == 1
	vec3 col = debugHeatRamp((f.temperature + 25.0) / 75.0) * debugContour(f.temperature, 5.0);
	const float snowLineC = u_terrainTexParams3.w; // the LIVE snow tweak, not a mirrored constant
	if (f.temperature <= snowLineC)
		col = mix(col, vec3(0.05), 0.75);
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
// Four physical layers composited bottom-up — no biome enum, climate selects textures directly:
//   1. GROUND — climate-picked soil/vegetation, world-XZ projection
//   2. BEACH  — shoreline band just above the local waterline (not climate-selected)
//   3. ROCK   — bedrock where too steep / too prominent to hold soil; climate-picked type, triplanar
//   4. SNOW   — cover over ALL of the above (snow falls ON bedrock — as a ground type the rock layer
//               would paint over it and peaks would come out gray)

struct TerrainSample
{
	vec3 albedo;
	vec3 normal;   // world space
	float rough;
	float metal;
	float ao;
};

// One splat material with world-XZ UVs; tangent basis = world X/Z reoriented onto the geometric
// normal. matIdx diverges between neighbouring pixels at climate borders -> nonuniformEXT.
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
	s.normal = normalize(tn.x * vec3(1.0, 0.0, 0.0) + tn.y * vec3(0.0, 0.0, 1.0) + tn.z * geoN);
	return s;
}

// Triplanar version for the rock layer (whiteout-style normal blend), so cliff faces don't smear.
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
	const vec3 nX = vec3(tnX.z * sign(geoN.x), tnX.y, tnX.x);
	const vec3 nY = vec3(tnY.x, tnY.z * sign(geoN.y), tnY.y);
	const vec3 nZ = vec3(tnZ.x, tnZ.y, tnZ.z * sign(geoN.z));
	s.normal = normalize(nX * w.x + nY * w.y + nZ * w.z + geoN * 2.0); // biased toward geoN: detail, not replacement
	return s;
}

// Crag wander noise (value fBm over world XZ). Lives HERE and not in the generator: the wander must be
// scaled by local relief, which the generator's coarse path cannot supply (its relief is 0 by
// construction — the cascades would disagree). The shader always has the true mesh height.
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

// ~[-1, 1], 3 octaves, amplitude-normalised.
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

// Match of `climate` against an entry's climate BOX: 1 inside, Gaussian-decaying outside. A box (not a
// point) lets an entry opt OUT of an axis by leaving that range at full width ("cold at ANY humidity").
float climateBoxWeight(vec2 climate, vec4 box, float invS2)
{
	const vec2 d = max(max(box.xz - climate, climate - box.yw), vec2(0.0));
	return exp(-dot(d, d) * invS2);
}

struct ClimatePick
{
	int i0;        // best entry (0-based like u_terrainSplatClimate; caller adds baseMat)
	int i1;        // runner-up
	float blend1;  // coverage of i1 over i0, in [0, 1]
};

// Top two climate entries in [first, first + count), weighted RELATIVE to the third (w - w2): when the
// #2/#3 ranking swaps, both candidates sit at zero contribution — otherwise every ranking crossover
// flips the second texture instantly and draws a hard seam.
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
	return ClimatePick(i0, i1, w1r / max((w0 - w2) + w1r, 1e-6)); // max: all weights can underflow to 0
}

// The full splatted terrain surface; neutral mid-gray before a texture set is registered.
TerrainSample terrainSplat(vec3 worldPos, vec3 geoN, TerrainFields f)
{
	const int baseMat = int(u_terrainTexParams0.x);
	const int numGround = int(u_terrainTexParams0.y);
	const int numRock = int(u_terrainTexParams0.z);
	if (baseMat < 0 || numGround <= 0)
		return TerrainSample(vec3(0.5), geoN, 0.92, 0.0, 1.0);

	// The baked temperature already carries the altitude lapse, so elevation enters the selection as
	// the cold it causes — snow line and vegetation cannot disagree.
	const vec2 climate = vec2(clamp((f.temperature + 25.0) / 75.0, 0.0, 1.0), f.humidity);
	const float invS2 = 1.0 / (2.0 * u_terrainTexParams0.w * u_terrainTexParams0.w);
	const float slope = 1.0 - clamp(geoN.y, 0.0, 1.0);
	const vec2 uvGround = worldPos.xz * u_terrainTexParams1.x;

	// --- 1. Ground
	const ClimatePick g = pickClimate(climate, 0, numGround, invS2);
	TerrainSample surf = sampleTerrainXZ(uint(baseMat + g.i0), uvGround, geoN);
	if (g.blend1 > 0.004)
		terrainMixInto(surf, sampleTerrainXZ(uint(baseMat + g.i1), uvGround, geoN), g.blend1);

	// --- 2. Beach: the band just above the local waterline.
	if (u_terrainTexParams3.x > 0.5)
	{
		const float beachW = 1.0 - smoothstep(0.3, max(u_terrainTexParams2.z, 0.31), worldPos.y - f.waterLevel);
		if (beachW > 0.004)
			terrainMixInto(surf, sampleTerrainXZ(uint(baseMat + numGround + numRock), uvGround, geoN), beachW);
	}

	// --- 3. Rock: too steep OR standing too far above the macro altitude (crag). max(), not a sum —
	// the two coincide on a cliff. On V3 terrain crag is what puts rock on mountains (the 30 m/px field
	// rarely reaches the slope threshold). The relief is wandered by fBm first or the rock boundary is
	// an elevation contour across a whole range; |wander| <= relief keeps flat lowlands untouched.
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

	// --- 4. Snow over everything, but it slides off steep faces (the mountain's rock shows through)
	// and needs humidity to fall at all (no white polar deserts).
	if (u_terrainTexParams3.y > 0.5)
	{
		const float cold  = 1.0 - smoothstep(u_terrainTexParams3.z, u_terrainTexParams3.w, f.temperature);
		const float holds = 1.0 - smoothstep(u_terrainTexParams4.x, u_terrainTexParams4.y, slope);
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

void main()
{
#ifdef STEREO
	g_viewIndex = int(u_viewIndex);
#endif
	const vec3 V = normalize(u_viewPos - in_pos);
	const vec3 geoN = normalize(in_tbn[2]); // geometric (interpolated vertex) normal
	const TerrainFields fields = terrainFields(in_pos);

#if TERRAIN_DEBUG_MODE != 0
	out_color = vec4(terrainDebugColor(fields, in_pos), 1.0); // unlit: the field itself, not its shading
	return;
#endif

	const TerrainSample surf = terrainSplat(in_pos, geoN, fields);
	// surf.ao = baked texture AO on top of the screen-space term (ambient/indirect only).
	const vec3 color = computeLitColor(in_pos, V, surf.normal, surf.albedo, surf.rough, surf.metal, surf.ao);
	out_color = vec4(color, 1.0); // opaque terrain
}
