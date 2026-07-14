export module Procedural:GeneratorV3;

import Core;
import :TerrainSampler;

// V3: the Terrain Diffusion generator (Private/Diffusion), exposed through the same ITerrainSampler the
// rest of the terrain stack consumes, so it drops in beside TerrainGenV2.
//
// HOW IT DIFFERS FROM V2, and why the shape of this class is what it is:
// V2 is a pure function of (x, z) — evaluate noise, done. V3 is a neural diffusion pipeline that generates
// 256x256-pixel TILES (7.68 km at the default 30 m/px) through three ONNX models. So:
//   * Sampling a point means bilinearly reading a resident tile. A miss BLOCKS the caller while the tile is
//     generated (~1.5 s cold; ~0 once resident, and one tile covers ~225 chunks at chunkSize 512).
//   * All inference is serialised onto one thread. That is required, not a convenience: the tile store is
//     not thread-safe, and sampleHeight is called from the terrain streamer worker, the scatter worker AND
//     the height-map baker's std::async.
//   * The models (2.28 GB) load once per process and are RESEEDED, never reloaded. Constructing a
//     TerrainGenV3 is therefore cheap; a tweak change that rebuilds the sampler costs nothing unless the
//     seed actually moved.
//   * The model resolves 30 m/px. Sub-pixel relief is procedural: a slope-masked noise layer rides on top,
//     which is what `sampleAltitude` (macro) vs `sampleHeight` (macro + detail) already models.
//
// NOT READY YET is a real state: until the models are downloaded and loaded, isReady() is false and the
// generator must not be used to build geometry (it would bake a flat world into the chunk cache).
// TerrainStreamer polls isReady() and rebuilds once it flips.
export namespace Procedural
{
	struct TerrainConfigV3
	{
		uint32 seed = 1337;
		float seaLevel = 0.0f;

		// --- World scale. The model natively resolves 30 m per pixel; that is what it was trained for and
		// what makes its continents continent-sized and its peaks ~10 km.
		// metersPerPixel is a UNIFORM scale: it shrinks elevation and detail by the same factor it shrinks
		// the horizontal, so a compressed world keeps the model's proportions — same slopes, same shapes,
		// same climate, just smaller and quicker to fly across. Lowering it is quadratically more expensive
		// (the same view distance then spans more model pixels, so more tiles must be generated).
		// heightScale is then a pure vertical exaggeration ON TOP: 1 = the model's real proportions.
		float metersPerPixel = 3.0f;
		float heightScale = 1.0f;

		// --- Sub-pixel detail. The diffusion field is smooth below 30 m, so without this a close-up
		// hillside is bilinear mush. Masked by SLOPE, so plains and seabed stay smooth and only mountain
		// faces get crags. Wavelengths are in metres and scale with metersPerPixel.
		float detailSlopeGain = 0.75f;   // sf = min(1, slope * gain); slope is m/m
		float detailWavelengthA = 220.0f; // coarse crags
		float detailAmplitudeA = 38.0f;
		uint32 detailOctavesA = 4;
		float detailWavelengthB = 45.0f;  // fine break-up
		float detailAmplitudeB = 11.0f;
		uint32 detailOctavesB = 3;

		// --- Climate mapping. The model emits precipitation in mm/year; ITerrainSampler wants humidity in
		// [0,1], so this is the precipitation that reads as fully humid.
		float precipForFullHumidity = 2200.0f;
		// Shifts the whole planet wetter/drier after that mapping, the humidity counterpart of
		// temperatureOffset. Biomes are picked by (temperature, humidity) proximity, so this slides the
		// world along the arid <-> lush axis: +0.2 turns steppe into forest, -0.2 turns it into desert.
		float humidityOffset = 0.0f; // added to the [0,1] humidity, then re-clamped

		// --- Temperature shaping. The model's temperatures are REAL (WorldClim-calibrated), and the biome
		// attractor table (BIOME_TABLE in GeneratorV2.cpp, shared with the terrain shader's splatting) is
		// expressed in real units to match, so the defaults here are ZERO: out of the box you get the
		// model's own climate and snow lands where the physics puts it.
		// These stay as artistic overrides:
		//   temperatureOffset  shifts the whole planet (a colder/warmer world)
		//   extraLapseRate     extra cooling with ALTITUDE, on top of the model's own regressed lapse rate.
		//                      A snow-line control: it scales with altitude, so it costs lowlands and ocean
		//                      nothing. Raise it to push snow further down the mountains than reality would.
		float temperatureOffset = 0.0f;  // degrees C
		float extraLapseRate = 0.0f;     // C per metre of altitude

		// --- Regional fog character (same role as V2's, derived from the model's real climate).
		float humidFogAmount = 0.6f;  // broad fog in wet regions
		float valleyFogAmount = 0.8f; // fog pooling below ridgelines

		// --- Residency. One tile is 256x256 native px + halo, ~800 KB across elevation/temperature/precip.
		int32 maxResidentTiles = 256;
	};

	class TerrainGenV3 final : public ITerrainSampler
	{
	public:
		explicit TerrainGenV3(const TerrainConfigV3& cfg);
		~TerrainGenV3() override;

		// Starts loading the models (which ship in Assets/TerrainDiffusion) onto the GPU on a background
		// thread. Cheap and idempotent; the TerrainGenV3 constructor calls it for you.
		static void beginLoad();
		// False while the models are loading, and permanently if that failed. Sampling before this is true
		// yields a flat sea-level world, so callers must gate geometry generation on it.
		static bool isReady();
		static bool hasFailed();
		// Short human-readable status, or the reason it failed.
		static std::string statusText();

		float sampleHeight(double worldX, double worldZ) const override;
		float sampleWaterHeight(double worldX, double worldZ) const override;
		float sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const override;
		float sampleTemperature(double worldX, double worldZ) const override;
		float sampleHumidity(double worldX, double worldZ) const override;
		float sampleFogThickness(double worldX, double worldZ) const override;
		float sampleFogHeightFalloff(double worldX, double worldZ) const override;
		// One evaluation for every field, and the only way to ask for the cheap coarse level (ESampleDetail).
		void samplePoint(double worldX, double worldZ, TerrainPoint& out,
		                 ESampleDetail detail = ESampleDetail::Full) const override;
		// Resolves the grid's whole tile set up front, then fills it without touching the shared cache.
		// Wide-area consumers should use this rather than looping samplePoint — on V3 the difference is one
		// lock per tile versus one per point.
		void sampleGrid(double originX, double originZ, double step, uint32 resX, uint32 resZ,
		                std::span<TerrainPoint> out, ESampleDetail detail = ESampleDetail::Full) const override;
		// The smooth 30 m/px diffusion surface WITHOUT the procedural detail layer — the terrain shader uses
		// this to tell "a mountain" from "high flatland".
		float sampleAltitude(double worldX, double worldZ) const override;
		float seaLevel() const override;

		const TerrainConfigV3& config() const;

	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
