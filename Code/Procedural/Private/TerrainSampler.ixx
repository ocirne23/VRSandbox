export module Procedural:TerrainSampler;

import Core;

export namespace Procedural
{
	// Encoding range of the baked 8-bit temperature channel: TEMPERATURE_MIN_C maps to 0, MAX_C to 255
	// (~0.29 C steps). Shared by the baker and the terrain shaders (terrain_height.inc.glsl mirrors it).
	inline constexpr float TEMPERATURE_MIN_C = -25.0f;
	inline constexpr float TEMPERATURE_MAX_C = 50.0f;
	// Encoding range of the baked 8-bit fog height-falloff MULTIPLIER: 0..FOG_FALLOFF_MUL_MAX maps to
	// 0..255 (1.0 = the global Fog/Height Falloff unchanged).
	inline constexpr float FOG_FALLOFF_MUL_MAX = 4.0f;

	// The point-evaluable terrain field every generator implements (V1 ClimateMaps, V2 TerrainGenV2).
	// All methods are pure functions of world position — seeded, thread-safe, world-continuous — which is
	// what keeps chunks, LODs, bakes (shore/fog maps) and buoyancy consistent with each other regardless
	// of which generator produced them. Consumers hold shared_ptr<const ITerrainSampler>.
	class ITerrainSampler
	{
	public:
		virtual ~ITerrainSampler() = default;

		// Terrain surface height in world meters (Y).
		virtual float sampleHeight(double worldX, double worldZ) const = 0;
		// Water surface height (world Y): the sea, plus whatever elevated water the generator models.
		// Water is present wherever this exceeds sampleHeight.
		virtual float sampleWaterHeight(double worldX, double worldZ) const = 0;
		// Both fields from one evaluation (bakes sampling both per texel use this).
		virtual float sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const
		{
			outWaterLevel = sampleWaterHeight(worldX, worldZ);
			return sampleHeight(worldX, worldZ);
		}
		virtual float sampleTemperature(double worldX, double worldZ) const = 0; // degrees CELSIUS (baked over TEMPERATURE_MIN_C..MAX_C)
		virtual float sampleHumidity(double worldX, double worldZ) const = 0;   // normalized [0,1] (baked 1:1 — 0 -> 0.0, 255 -> 1.0)
		virtual float sampleFogThickness(double worldX, double worldZ) const = 0; // [0,1] regional fog multiplier
		// Regional multiplier on the global Fog/Height Falloff, [0, FOG_FALLOFF_MUL_MAX] (1 = neutral):
		// lets a generator make fog hug the ground in one region and tower in another.
		virtual float sampleFogHeightFalloff(double, double) const { return 1.0f; }
		// Directed water-flow angle at (x, z), as angle / 2pi in [0,1) (0 = +X, counter-clockwise), or
		// < 0 where the water has no direction (open ocean/lakes -> the global wind drives the waves).
		// Baked into the ocean shore map so the water shader orients its wave field along rivers.
		virtual float sampleFlowAngle01(double, double) const { return -1.0f; }
		// The MACRO elevation (m above sea level) before fine detail — what separates "a mountain" (height
		// far above the local altitude) from "high-altitude flatland" (height ~ altitude). Fed per-vertex
		// to the terrain shader for coloring. Default: the full height (no macro/detail split).
		virtual float sampleAltitude(double worldX, double worldZ) const { return sampleHeight(worldX, worldZ) - seaLevel(); }
		virtual float seaLevel() const = 0;
	};
}
