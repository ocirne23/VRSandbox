export module Procedural:TerrainSampler;

import Core;

export namespace Procedural
{
	// Encoding range of the baked 8-bit temperature channel: TEMPERATURE_MIN_C maps to 0, MAX_C to 255
	// (~0.29 C steps). Shared by the baker and the terrain shaders (terrain_height.inc.glsl mirrors it).
	inline constexpr float TEMPERATURE_MIN_C = -25.0f;
	inline constexpr float TEMPERATURE_MAX_C = 50.0f;

	// Humidity encoding: sampleHumidity is [0,1], where 1 means HUMIDITY_FULL_PRECIP_MM of annual
	// precipitation or more. A generator that models real precipitation (V3, whose diffusion model emits
	// mm/yr) maps through this; V2's humidity is a normalized field that adopts it implicitly.
	inline constexpr float HUMIDITY_FULL_PRECIP_MM = 2200.0f;

	// Physical climate -> the normalized (t01, h01) space that the biome attractor table, V2's
	// terrain-character blend and the terrain shader's texture splatting ALL share. The shader mirrors
	// temperatureTo01 as clamp((temp + 25) / 75) — keep them in step.
	constexpr float temperatureTo01(float celsius)
	{
		constexpr float invRange = 1.0f / (TEMPERATURE_MAX_C - TEMPERATURE_MIN_C);
		const float t = (celsius - TEMPERATURE_MIN_C) * invRange;
		return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	}
	constexpr float precipTo01(float mmPerYear)
	{
		constexpr float invFull = 1.0f / HUMIDITY_FULL_PRECIP_MM;
		const float h = mmPerYear * invFull;
		return h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
	}
	// Encoding range of the baked 8-bit fog height-falloff MULTIPLIER: 0..FOG_FALLOFF_MUL_MAX maps to
	// 0..255 (1.0 = the global Fog/Height Falloff unchanged).
	// NOT baked any more — it is a pure function of temperature, so the shaders recompute it (see
	// fogFalloffFromTemperature) and its 8 bits in the packed channel now carry the LAPSE RATE, which
	// nothing else can reconstruct. Kept because both sides still need the range to agree.
	inline constexpr float FOG_FALLOFF_MUL_MAX = 4.0f;

	// Encoding range of the baked 8-bit LAPSE RATE (temperature change per metre of elevation, in the
	// generator's own vertical frame). Matches the clamp the diffusion pipeline's own regression applies.
	// This is baked because the terrain-data map's two cascades report temperature at DIFFERENT elevations
	// — the near one at the full-detail surface, the far one at its 7.68 km average, which cannot know a
	// peak's real height — so the shader has to re-derive temperature at the mesh elevation to make them
	// agree. Without it, distant peaks read up to 10.6 C too warm and their textures flip.
	inline constexpr float LAPSE_RATE_MIN = -0.012f;
	inline constexpr float LAPSE_RATE_MAX = 0.0f;

	// Fog height-falloff from temperature: cold air hugs the ground, warm air lets fog tower. Mirrored by
	// terrain_height.inc.glsl — keep them in step.
	constexpr float fogFalloffFromTemperature(float celsius)
	{
		const float t01 = (celsius + 10.0f) * (1.0f / 40.0f);
		const float c = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);
		const float f = 1.8f - 1.2f * c;
		return f < 0.0f ? 0.0f : (f > FOG_FALLOFF_MUL_MAX ? FOG_FALLOFF_MUL_MAX : f);
	}

	// Every field the terrain-data bake needs, from ONE evaluation. Sampling these one at a time is fine
	// for a noise field but not for a generator with a per-point cost (V3 does a tile lookup + bilinear per
	// call, so the six separate calls were six times the work).
	struct TerrainPoint
	{
		float height = 0.0f;         // surface Y (m)
		float waterLevel = 0.0f;     // water surface Y (m)
		float altitude = 0.0f;       // macro elevation above sea level (m)
		float temperature = 15.0f;   // degrees CELSIUS
		float humidity = 0.5f;       // [0,1]
		float fogThickness = 0.0f;   // [0,1]
		float fogFalloffMul = 1.0f;  // [0, FOG_FALLOFF_MUL_MAX], 1 = neutral. NOT baked (derived from
		                             // temperature); still reported for CPU consumers.
		// Temperature change per metre of elevation IN THE GENERATOR'S OWN VERTICAL FRAME — for V3 that is
		// model metres, so a consumer working in world metres must divide by vertScale (which the terrain
		// shader gets as u_terrainParams.y). Not pre-converted, because the world-frame rate leaves the
		// [LAPSE_RATE_MIN, LAPSE_RATE_MAX] range this is encoded over as soon as the world is compressed.
		// `temperature` above is the value at THIS point's own height; this is what lets a consumer move it
		// to a different height. See LAPSE_RATE_MIN for why the terrain shader must.
		float lapseRate = -0.0065f;
		float flowAngle01 = -1.0f;   // angle/2pi in [0,1), or < 0 for no direction
	};

	// How much fidelity a query needs.
	//   Full   — the real field, whatever it costs. Geometry and near-field bakes.
	//   Coarse — a cheap, low-resolution approximation, for bakes that span tens of km at low texel
	//            density. A generator whose field is built hierarchically may answer these from a much
	//            cheaper level; one that is a plain function of (x, z) just ignores the hint.
	// This exists because V3 (diffusion) generates 7.68 km TILES at real cost: a far cascade covering the
	// whole view distance would otherwise force thousands of full-detail tiles — for terrain largely beyond
	// the mesh ring — and evict the ones the geometry needs. V3 answers Coarse from its coarse stage alone,
	// where one tile covers several hundred km. The shader crossfades near->far, so the drop in fidelity
	// blends in rather than seaming.
	enum class ESampleDetail : uint8
	{
		Full,
		Coarse
	};

	// The point-evaluable terrain field every generator implements (TerrainGenV2, TerrainGenV3).
	// All methods are pure functions of world position — seeded, thread-safe, world-continuous — which is
	// what keeps chunks, LODs, bakes (shore/fog maps) and buoyancy consistent with each other regardless
	// of which generator produced them. Consumers hold shared_ptr<const ITerrainSampler>.
	class ITerrainSampler
	{
	public:
		virtual ~ITerrainSampler() = default;

		// Everything at once. The default composes the individual samplers, which is right for a pure
		// function of (x, z); override when a shared evaluation is cheaper, or when Coarse can be answered
		// from a cheaper level.
		virtual void samplePoint(double worldX, double worldZ, TerrainPoint& out,
		                         ESampleDetail = ESampleDetail::Full) const
		{
			out.height = sampleHeightAndWater(worldX, worldZ, out.waterLevel);
			out.altitude = sampleAltitude(worldX, worldZ);
			out.temperature = sampleTemperature(worldX, worldZ);
			out.humidity = sampleHumidity(worldX, worldZ);
			out.fogThickness = sampleFogThickness(worldX, worldZ);
			out.fogFalloffMul = sampleFogHeightFalloff(worldX, worldZ);
			out.flowAngle01 = sampleFlowAngle01(worldX, worldZ);
		}

		// A regular XZ grid: point (i, j) sits at (originX + i*step, originZ + j*step) and is written to
		// out[j*resX + i]. out must hold resX*resZ entries.
		//
		// This exists so a generator can hoist its PER-POINT overhead out of the loop. For a noise field the
		// default loop is exactly right and costs nothing. For V3 it is the difference between a shared-cache
		// lock per point and one per tile: the terrain-data bake is 512x512 texels per cascade, so the point
		// path meant a quarter-million lock/unlock pairs contending with the mesh worker for a grid that
		// resolves to a couple of dozen tiles. Wide-area consumers should prefer this over samplePoint.
		virtual void sampleGrid(double originX, double originZ, double step, uint32 resX, uint32 resZ,
		                        std::span<TerrainPoint> out, ESampleDetail detail = ESampleDetail::Full) const
		{
			assert(out.size() >= (size_t)resX * resZ);
			for (uint32 j = 0; j < resZ; j++)
				for (uint32 i = 0; i < resX; i++)
					samplePoint(originX + i * step, originZ + j * step, out[(size_t)j * resX + i], detail);
		}

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
