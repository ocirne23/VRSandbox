export module Procedural:GeneratorV2;

import Core;
import Core.glm;
import :Noise;
import :TerrainSampler;

export namespace Procedural
{
	// V2 biome palette — a CLASSIFICATION, not a generative step: temperature/humidity/continentalness are
	// generated as INDEPENDENT world-continuous fields, and each land biome is an attractor at its
	// (temperature, humidity) coordinates in climate space (BIOME_TABLE). Terrain character (base height,
	// roughness, relief) blends between the attractors nearest the local climate, so "biomes" emerge where
	// the climate fields land near their coordinates — desert borders jungle only where hot-dry actually
	// meets hot-wet. sampleBiome reports the nearest attractor (debug/gameplay).
	enum class EBiomeV2 : uint8
	{
		Ocean,     // spatial, not climatic: continentalness below the ocean threshold
		Desert,
		Savanna,
		Swampland,
		Grassland,
		Forest,
		Rainforest,
		Taiga,
		Tundra,
		Arctic,
		Lakes,     // the ultra-humid attractor: depressed + dead flat (reads as wetland basins)
		Highlands, // elevated plateaus; climate weight additionally gated by inland-ness (deep interior only)
		Count
	};

	// The (temperature01, humidity01) coordinates of a biome's attractor in climate space (BIOME_TABLE) —
	// for consumers that must agree with the generator's biome placement (terrain texture splatting).
	glm::vec2 biomeClimateCoords(EBiomeV2 biome);

	struct TerrainConfigV2
	{
		uint32 seed = 1337;
		float  seaLevel = 0.0f;

		// --- The independent climate fields. Temperature and humidity are separate low-frequency fBm
		// fields (temperature additionally carries the continent-scale hot/cold bands), continentalness a
		// third; all three share one domain warp so coastlines and climate contours wiggle together.
		float  biomeSize = 1600.0f;          // typical biome extent (m): the climate fields' wavelength is
		                                     // ~4x this (one wavelength sweeps across several attractors)
		float  biomeBlend = 1.0f;            // climate-space kernel width: how softly terrain character
		                                     // blends between biome attractors (low = crisp biome identity)
		float  borderWarp = 350.0f;          // domain warp on the field lookups (m): wiggly, natural
		                                     // borders/coasts (sanitized to <= 2x biomeSize)
		float  oceanFraction = 0.32f;        // fraction of the world below the continent threshold = Ocean
		float  continentFrequency = 0.00005f; // continent/ocean scale (cycles/m, ~25km): few, HUGE oceans

		// Continent-scale altitude gradient: land rises with continentalness above the ocean threshold
		// (deep inland = high; ramps down to meet the coast) and the seabed deepens away from shore. Biome
		// base heights are OFFSETS on this gradient, so any biome (swamp, wetland...) can sit at altitude.
		float  inlandRise = 120.0f;          // altitude gain (m) from coast to deepest inland
		float  oceanDeepen = 1000.0f;          // extra seabed depth (m) from shore to mid-ocean

		// --- The altitude map. A mid-resolution lattice between the climate fields and the fine height
		// field: each texel = the climate-blended base elevation + large-scale relief noise, sampled with a
		// bicubic B-spline so the macro height gradients are genuinely smooth (C2). The fine height field
		// rides on it (adding only climate-blended roughness), and future passes can plan rivers on it.
		float  altitudeTexelSize = 64.0f;    // texel size (m): far coarser than the fine fields
		float  altitudeFrequency = 0.0002f;  // large-scale relief noise (cycles/m, ~5km features)
		uint32 altitudeOctaves = 3;
		float  altitudeAmplitude = 60.0f;    // rolling-relief swing (m) on top of the blended base heights

		// Fantasy peaks: a low-frequency ridged field pushed through a threshold + power curve — most land
		// stays rolling, but where the field clears the threshold big ranges erupt. The climate-blended
		// relief scale damps all relief in flat biomes (grasslands/swamps stay smooth).
		float  peakFrequency = 0.00025f;     // range placement scale (~4km features)
		float  peakAmplitude = 250.0f;       // peak height (m) at full curve
		float  peakThreshold = 0.5f;         // ridged-field value where peaks start (higher = rarer ranges)
		float  peakSharpness = 1.8f;         // power on the curve (higher = steeper, more dramatic peaks)

		// Altitude-aware mountain detail: high-frequency ridged relief whose amplitude follows the
		// altitude map's LOCAL RELIEF — how far the macro surface rises above its own baseline (climate
		// base height + continent gradient) — NOT the raw elevation: the smooth B-spline ranges grow
		// craggy ridgelines and side valleys at any altitude, while flat ground (grasslands, high inland
		// plateaus lifted by the continent gradient) never sees any of it.
		float  mountainDetailFrequency = 0.004f; // ridge detail scale (~250m features)
		uint32 mountainDetailOctaves = 4;
		float  mountainDetailAmplitude = 90.0f;  // ridge relief (m) at full mask
		float  mountainMaskStart = 20.0f;        // macro relief (m above the local baseline) where the detail fades in
		float  mountainMaskFull = 120.0f;        // macro relief where it reaches full amplitude

		// --- Fine fields.
		float  detailFrequency = 0.008f;     // height detail fBm (cycles/m)
		uint32 detailOctaves = 5;
		float  heightScale = 1.0f;           // global multiplier on altitude + blended height table
		float  climateNoise = 0.08f;         // +- jitter on the REPORTED temperature/humidity (breaks up
		                                     // flatness; the generative fields stay smooth)
		float  climateBandFrequency = 0.00003f; // hot/cold region scale (~30km), folded into temperature
		float  climateBandAmplitude = 0.25f;    // +- temperature swing of the bands
		float  temperatureLapse = 0.0012f;      // temperature drop per meter of altitude (snowy peaks)

		// --- Regional fog character (baked into the terrain-data map's packed channel; applied by the
		// volumetric fog via Fog/Region strength on top of the global density/falloff).
		float  grassFogAmount = 0.5f;  // peak thickness of the grassland low-fog patches (0 disables)
		float  valleyFogAmount = 0.8f; // peak thickness of mountain-valley fog (0 disables)
	};

	// V2 climate-first generator: temperature / humidity / continentalness are generated as independent
	// seeded fields; every other quantity derives from their combination — terrain character by blending
	// the biome attractor table in climate space, the macro altitude lattice from continentalness + that
	// character, the fine height from altitude + character-scaled detail. Everything is a deterministic
	// function of (x, z); geometry (TerrainStreamer chunks) consumes it through ITerrainSampler.
	class TerrainGenV2 final : public ITerrainSampler
	{
	public:
		explicit TerrainGenV2(const TerrainConfigV2& cfg);

		// Classification: the nearest biome attractor to the local climate (Ocean below the continent
		// threshold; Highlands only deep inland). Debug/gameplay — generation never branches on it.
		EBiomeV2 sampleBiome(double worldX, double worldZ) const;

		// The smooth macro elevation in meters relative to sea level (bicubic B-spline over the altitude
		// lattice, heightScale applied). The fine height field adds only climate-blended roughness on top;
		// also fed per-vertex to the terrain shader (mountain-vs-high-flatland coloring).
		float sampleAltitude(double worldX, double worldZ) const override;

		float sampleHeight(double worldX, double worldZ) const override;
		float sampleWaterHeight(double worldX, double worldZ) const override; // sea level (no elevated water yet)
		float sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const override;
		float sampleTemperature(double worldX, double worldZ) const override;
		float sampleHumidity(double worldX, double worldZ) const override;
		// Fog character, derived from the other fields (see fogAt): humid regions carry broad fog,
		// grasslands grow patchy ground-hugging mist, mountain valleys fill below their ridgelines.
		float sampleFogThickness(double worldX, double worldZ) const override;
		float sampleFogHeightFalloff(double worldX, double worldZ) const override;
		float seaLevel() const override { return m_cfg.seaLevel; }

		const TerrainConfigV2& config() const { return m_cfg; }

	private:
		// The three independent fields at one point, evaluated through one shared domain warp:
		// c = continentalness [0,1] (oceans below oceanFraction), t01/h01 = temperature/humidity [0,1].
		struct Climate { float c, t01, h01; };
		Climate climateAt(double worldX, double worldZ) const;

		// Terrain character from the climate: Gaussian-weighted blend of the biome attractor table in
		// (t01, h01) space, mixed toward the Ocean row across the coastal continentalness band.
		struct FieldBlend { float baseHeight, heightVar, reliefScale; };
		FieldBlend blendFields(const Climate& cl) const;

		float altitudeAtTexel(int32 ax, int32 az) const; // altitude lattice value, memoized
		// The continent-scale altitude gradient (coastal shelf + inland rise + ocean deepen) at
		// continentalness c — part of the altitude map's BASELINE (with the climate base height).
		float continentGradient(float c) const;
		// The altitude map's local relief: macro altitude minus its baseline. This is what "mountain-ness"
		// means — the mountain-detail mask and the valley fog key on it, not on raw elevation.
		float macroRelief(float sampledAltitude, const Climate& cl, const FieldBlend& blend) const;

		// Both fog fields from one evaluation (they share the climate/valley analysis).
		struct FogSample { float thickness, falloffMul; };
		FogSample fogAt(double worldX, double worldZ) const;

		TerrainConfigV2 m_cfg;
		uint32 m_instanceId = 0; // keys the thread-local altitude-texel cache

		NoiseField m_continent; // ocean/landmass mask
		NoiseField m_warpA;     // shared domain warp X
		NoiseField m_warpB;     // shared domain warp Z
		NoiseField m_tempField; // temperature climate field
		NoiseField m_humField;  // humidity climate field
		NoiseField m_altitude;  // large-scale rolling relief
		NoiseField m_peaks;     // ridged peak field (fantasy ranges)
		NoiseField m_mountain;  // altitude-masked ridge detail
		NoiseField m_detail;    // height detail
		NoiseField m_climT;     // temperature bands + reported-value jitter
		NoiseField m_climH;     // humidity reported-value jitter
		NoiseField m_fogPatch;  // grassland low-fog patch placement
	};
}
