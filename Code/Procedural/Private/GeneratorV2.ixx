export module Procedural:GeneratorV2;

import Core;
import Core.glm;
import :Noise;
import :TerrainSampler;

export namespace Procedural
{
	// V2 biome palette (pass 1 output). Placement is deliberately unrealistic — any biome can neighbor any
	// other (desert next to tundra is fine); coherence comes from cluster size, not climate simulation.
	enum class EBiomeV2 : uint8
	{
		Ocean,
		Desert,
		Savanna,
		Swampland,
		Grassland,
		Forest,
		Rainforest,
		Taiga,
		Tundra,
		Arctic,
		Lakes,     // depressed + dead flat + saturated: pass-3 depression fill floods it into real lakes
		Highlands, // elevated plateaus (high-altitude flatland, NOT crag — the altitude/height split shows this)
		Count
	};

	struct TerrainConfigV2
	{
		uint32 seed = 1337;
		float  seaLevel = 0.0f;

		// --- Pass 1: the biome field (a conceptual infinite low-res uint8 texture, one biome index per
		// texel, hard borders / no blending). Oceans come from a very-low-frequency continent noise so
		// they form large coherent bodies; land texels pick a biome from a jittered Voronoi cluster.
		// The three biome-field knobs (texel size DERIVES from size/resolution, so it can never fall out
		// of sync with the biome scale — the old separate texel tweak aliased tiny clusters into speckle):
		float  biomeSize = 1600.0f;          // typical biome extent (m): the Voronoi cluster spacing
		float  biomeResolution = 12.0f;      // biome-field texels across one biome (texel = size/resolution)
		float  biomeBlend = 2.0f;            // blend-kernel width in texels: drives the gradient width of
		                                     // EVERY derived field (temp/humidity/height/relief)
		float  borderWarp = 350.0f;          // domain warp on the lookup (m): wiggly, natural borders
		                                     // (sanitized to <= 2x biomeSize)
		float  oceanFraction = 0.32f;        // fraction of the world below the continent threshold = Ocean
		float  continentFrequency = 0.00004f; // continent/ocean scale (cycles/m, ~25km): few, HUGE oceans

		// Continent-scale altitude gradient: land rises with continentalness above the ocean threshold
		// (deep inland = high; ramps down to meet the coast) and the seabed deepens away from shore. Biome
		// base heights are OFFSETS on this gradient, so any biome (swamp, lakes...) can sit at altitude.
		float  inlandRise = 120.0f;          // altitude gain (m) from coast to deepest inland
		float  oceanDeepen = 40.0f;          // extra seabed depth (m) from shore to mid-ocean

		// --- Pass 1.5: the altitude map. A mid-resolution field between the biome map and the fine
		// fields: each texel = the biome-blended base elevation + large-scale relief noise, sampled with a
		// bicubic B-spline so the macro height gradients are genuinely smooth (C2). The fine height field
		// rides on it (adding only per-biome roughness), and future passes can plan rivers on it.
		float  altitudeTexelSize = 64.0f;    // texel size (m): finer than the biome map, far coarser than the fine fields
		float  altitudeFrequency = 0.0002f;  // large-scale relief noise (cycles/m, ~5km features)
		uint32 altitudeOctaves = 3;
		float  altitudeAmplitude = 60.0f;    // rolling-relief swing (m) on top of the biome base heights

		// Fantasy peaks: a low-frequency ridged field pushed through a threshold + power curve — most land
		// stays rolling, but where the field clears the threshold big ranges erupt. Per-biome relief scale
		// (BIOME_TABLE) damps all relief in flat biomes (grasslands/swamps stay smooth).
		float  peakFrequency = 0.00025f;     // range placement scale (~4km features)
		float  peakAmplitude = 250.0f;       // peak height (m) at full curve
		float  peakThreshold = 0.5f;         // ridged-field value where peaks start (higher = rarer ranges)
		float  peakSharpness = 1.8f;         // power on the curve (higher = steeper, more dramatic peaks)

		// Altitude-aware mountain detail (pass 2): high-frequency ridged relief whose amplitude follows
		// the MACRO altitude — the smooth B-spline ranges from pass 1.5 grow craggy ridgelines and side
		// valleys, while low ground never sees any of it (smooth grasslands preserved).
		float  mountainDetailFrequency = 0.004f; // ridge detail scale (~250m features)
		uint32 mountainDetailOctaves = 4;
		float  mountainDetailAmplitude = 90.0f;  // ridge relief (m) at full mask
		float  mountainMaskStart = 20.0f;        // altitude above sea (m) where the detail fades in
		float  mountainMaskFull = 120.0f;        // altitude where it reaches full amplitude

		// --- Pass 2: derived fields (temperature / humidity / height). Each blends the per-biome table
		// values over the neighboring biome texels (smooth kernel, ~2 texels wide) and adds seeded noise.
		float  detailFrequency = 0.008f;     // height detail fBm (cycles/m)
		uint32 detailOctaves = 5;
		float  heightScale = 1.0f;           // global multiplier on altitude + biome height table
		float  climateNoise = 0.08f;         // +- noise on temperature/humidity (breaks up table flatness)
		// Temperature layer: continent-scale hot/cold bands + altitude lapse (high ground is cold — snow
		// on peaks and cold Highlands regardless of biome). Humidity separates desert/jungle (both hot).
		float  climateBandFrequency = 0.00003f; // hot/cold region scale (~30km)
		float  climateBandAmplitude = 0.25f;    // +- temperature swing of the bands
		float  temperatureLapse = 0.0012f;      // temperature drop per meter of altitude

		// --- Optional pass 3: water accumulation + erosion. Simulated on seeded region tiles (rain ->
		// steepest-descent flow routing -> stream-power channel carve -> depression fill), then applied as
		// height deltas + a humidity boost (humidity -> 1 marks rivers/lakes). Deltas feather to zero at
		// tile borders, so rivers stay within a region for now (no cross-tile continuity needed).
		bool   erosionEnabled = true;
		float  erosionRegionSize = 2048.0f;  // simulation tile size (m)
		float  erosionStrength = 6.0f;       // max channel carve depth (m); 0 disables too
	};

	struct ErosionTileV2; // simulated region tile (internal; cached per generator)

	// V2 "Minecraft-style" layered generator: PASS 1 makes a low-res biome index field; PASS 2 derives
	// temperature / humidity / height as higher-res fields that blend the biome table across neighboring
	// texels (which is why the biome field being low-res is useful — 16 cached texel lookups cover the
	// blend); PASS 3 (optional) runs a water accumulation + erosion simulation per region tile, carving
	// river channels into the height field and pushing humidity toward 1 over rivers/lakes; the geometry
	// pass (TerrainStreamer chunks) then consumes the fields through ITerrainSampler. Everything is a
	// seeded, deterministic function of (x, z). Rendered water is still the Ocean sea level only.
	class TerrainGenV2 final : public ITerrainSampler
	{
	public:
		explicit TerrainGenV2(const TerrainConfigV2& cfg);

		// Pass 1: the biome field, quantized to biomeTexelSize texels (hard borders, uint8 index).
		EBiomeV2 sampleBiome(double worldX, double worldZ) const;

		// Pass 1.5: the smooth macro elevation in meters relative to sea level (bicubic B-spline over the
		// altitude lattice, heightScale applied). The fine height field adds only per-biome roughness on
		// top of this; also fed per-vertex to the terrain shader (mountain-vs-high-flatland coloring) and
		// exposed for future passes (river planning follows these gradients).
		float sampleAltitude(double worldX, double worldZ) const override;

		// Pass 2 / ITerrainSampler:
		float sampleHeight(double worldX, double worldZ) const override;
		// Sea level everywhere, lifted onto the pass-3 river/lake surface where the simulation put water
		// — the ocean renderer draws rivers and lakes through the same shore-map plumbing as the sea.
		float sampleWaterHeight(double worldX, double worldZ) const override;
		float sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const override;
		float sampleTemperature(double worldX, double worldZ) const override;
		float sampleHumidity(double worldX, double worldZ) const override;
		float sampleFogThickness(double worldX, double worldZ) const override; // derived: humid biomes are foggy
		float sampleFlowAngle01(double worldX, double worldZ) const override;  // pass-3 river flow direction
		float seaLevel() const override { return m_cfg.seaLevel; }

		const TerrainConfigV2& config() const { return m_cfg; }

	private:
		float continentalness(double worldX, double worldZ) const;  // warped [0,1] field: oceans below oceanFraction
		EBiomeV2 biomeRaw(double worldX, double worldZ) const;      // un-quantized pass-1 evaluation
		EBiomeV2 biomeAtTexel(int32 tx, int32 tz) const;            // texel-center lookup, memoized
		float altitudeAtTexel(int32 ax, int32 az) const;            // pass-1.5 lattice value, memoized

		// Per-biome table values blended over the 4x4 biome texels around a point (smooth ~2-texel kernel):
		// the "takes the biome AND its neighbors into consideration" part of every pass-2 field.
		struct FieldBlend { float temperature, humidity, baseHeight, heightVar, reliefScale; };
		FieldBlend blendFields(double worldX, double worldZ) const;

		// Pass-2 fields BEFORE the erosion pass (pass 3 simulates its tiles from these).
		float baseHeight(double worldX, double worldZ) const;
		float baseHumidity(double worldX, double worldZ) const;

		// Pass 3: cached tile lookup + bilinear application.
		struct ErosionSample
		{
			float deltaH = 0.0f;     // height delta (<= 0, channel carve)
			float waterBoost = 0.0f; // [0,1] river/lake presence (drives humidity -> 1)
			float waterY = -30.0f;   // water surface relative to the pass-2 base height (dry = deep below)
			float flow01 = -1.0f;    // river flow angle / 2pi in [0,1); < 0 = no directed flow
		};
		std::shared_ptr<const ErosionTileV2> getErosionTile(int32 tileX, int32 tileZ) const;
		ErosionSample erosionAt(double worldX, double worldZ) const;

		TerrainConfigV2 m_cfg;
		float m_texelSize = 128.0f; // derived in the ctor: biomeSize / biomeResolution, clamped
		uint32 m_instanceId = 0;    // keys the thread-local biome-texel cache

		// Simulated erosion tiles, built lazily on first sample (deterministic, so a rare duplicate build
		// under contention is harmless). Guarded by the mutex; hot samples go through a thread-local
		// last-tile memo instead of the map.
		mutable std::mutex m_erosionMutex;
		mutable std::unordered_map<uint64, std::shared_ptr<const ErosionTileV2>> m_erosionTiles;

		NoiseField m_continent; // ocean/landmass mask
		NoiseField m_warpA;     // border warp X
		NoiseField m_warpB;     // border warp Z
		NoiseField m_altitude;  // pass-1.5 large-scale rolling relief
		NoiseField m_peaks;     // pass-1.5 ridged peak field (fantasy ranges)
		NoiseField m_mountain;  // pass-2 altitude-masked ridge detail
		NoiseField m_detail;    // height detail
		NoiseField m_climT;     // temperature jitter
		NoiseField m_climH;     // humidity jitter
	};
}
