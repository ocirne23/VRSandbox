module Procedural;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;
import Core.Tweaks;
import Core.Log;

import RendererVK;
import File;
import Spatial;

import :TerrainStreamer;
import :TerrainSampler;
import :GeneratorV2;
import :TerrainGenerator;
import :TerrainChunk;

namespace
{
	// GEOMETRIC LOD bands: lod = floor(log2(1 + cheb/lodStep)) — lodStep rings of LOD0, then 2*lodStep
	// of LOD1, 4*lodStep of LOD2, ... capped at maxLod. Each LOD halves mesh density while a feature's
	// screen size halves per distance DOUBLING, so doubling band widths keeps the on-screen triangle
	// density roughly constant (linear bands over-detailed the mid rings). This is THE ring-LOD function:
	// enqueue, queue staleness, result validation and eviction all derive from it.
	uint32 ringLodAt(int cheb, float lodStep, uint32 maxLod)
	{
		// Float step: fractional values place band boundaries between rings (steps < 1 pull the coarser
		// bands inside the first rings). Deterministic across all callers — same function, same inputs.
		const float k = (float)cheb / glm::max(lodStep, 0.01f);
		const int lod = (int)std::floor(std::log2(1.0f + k));
		return glm::min(maxLod, (uint32)glm::max(lod, 0));
	}

	// Pack a chunk coordinate + LOD into a stable 64-bit key. 28 bits each for X/Z covers +-134M chunks.
	uint64 chunkKey(glm::ivec2 coord, uint32 lod)
	{
		const uint64 x = (uint64)(uint32)coord.x & 0xFFFFFFFull;
		const uint64 z = (uint64)(uint32)coord.y & 0xFFFFFFFull;
		return (x << 36) | (z << 8) | (uint64)(lod & 0xFFu);
	}

	// --- Terrain splat texture sources -----------------------------------------------------------------
	// One PBR texture set per land biome (climate-blended ground), per climate-picked rock type (steep
	// slopes/crags), and exactly one dedicated Beach set — NOT a biome, a climate-independent shoreline
	// overlay the shader blends in near the waterline regardless of which biome it's over. Sources are
	// the CC0 sets under Assets/Textures/Terrain (see Assets/THIRD_PARTY_ASSETS.md); they bake once into
	// Assets/Local/TerrainTex as BC .dds so they mip-stream like cooked scene textures.
	enum class ESourceKind : uint8 { Ground, Rock, Beach };
	struct TerrainTexSource
	{
		ESourceKind kind = ESourceKind::Ground;
		Procedural::EBiomeV2 biome = Procedural::EBiomeV2::Count; // Ground entries only
		// Rock entries only: the attractor's climate, in the same REAL units as GeneratorV2's BIOME_TABLE
		// (mean annual temperature C / annual precipitation mm) — converted to (t01, h01) at use. These
		// must stay aligned with the ground attractors or a rock type simply never gets picked.
		float rockTempC = 0.0f;
		float rockPrecipMm = 0.0f;
		const char* cacheName;       // output stem: Local/TerrainTex/<cacheName>_{diff|nor|arm}.dds
		const char* diffSrc;         // sources relative to Assets/
		const char* norSrc;
		const char* armSrc;          // packed AO/rough/metal; nullptr -> pack aoSrc+roughSrc instead
		const char* aoSrc = nullptr;
		const char* roughSrc = nullptr;
	};
	using enum Procedural::EBiomeV2;
	const TerrainTexSource TERRAIN_TEX_SOURCES[] =
	{
		{ .biome = Desert,     .cacheName = "desert",     .diffSrc = "Textures/Terrain/sand_01/sand_01_diff_2k.jpg",                   .norSrc = "Textures/Terrain/sand_01/sand_01_nor_gl_2k.jpg",                   .armSrc = "Textures/Terrain/sand_01/sand_01_arm_2k.jpg" },
		{ .biome = Savanna,    .cacheName = "savanna",    .diffSrc = "Textures/Terrain/dry_ground_01/dry_ground_01_diff_2k.jpg",       .norSrc = "Textures/Terrain/dry_ground_01/dry_ground_01_nor_gl_2k.jpg",       .armSrc = "Textures/Terrain/dry_ground_01/dry_ground_01_arm_2k.jpg" },
		{ .biome = Swampland,  .cacheName = "swampland",  .diffSrc = "Textures/Terrain/mud_forest/mud_forest_diff_2k.jpg",             .norSrc = "Textures/Terrain/mud_forest/mud_forest_nor_gl_2k.jpg",             .armSrc = "Textures/Terrain/mud_forest/mud_forest_arm_2k.jpg" },
		{ .biome = Grassland,  .cacheName = "grassland",  .diffSrc = "Textures/Terrain/Grass001/Grass001_2K-JPG_Color.jpg",            .norSrc = "Textures/Terrain/Grass001/Grass001_2K-JPG_NormalGL.jpg",           .armSrc = "Textures/Terrain/Grass001/Grass001_arm_2k.jpg" },
		{ .biome = Forest,     .cacheName = "forest",     .diffSrc = "Textures/Terrain/forest_floor/forest_floor_diff_2k.jpg",         .norSrc = "Textures/Terrain/forest_floor/forest_floor_nor_gl_2k.jpg",         .armSrc = "Textures/Terrain/forest_floor/forest_floor_arm_2k.jpg" },
		{ .biome = Rainforest, .cacheName = "rainforest", .diffSrc = "Textures/Terrain/Moss002/Moss002_2K-JPG_Color.jpg",              .norSrc = "Textures/Terrain/Moss002/Moss002_2K-JPG_NormalGL.jpg",             .armSrc = "Textures/Terrain/Moss002/Moss002_arm_2k.jpg" },
		{ .biome = Taiga,      .cacheName = "taiga",      .diffSrc = "Textures/Terrain/forest_ground_04/forest_ground_04_diff_2k.jpg", .norSrc = "Textures/Terrain/forest_ground_04/forest_ground_04_nor_gl_2k.jpg", .armSrc = "Textures/Terrain/forest_ground_04/forest_ground_04_arm_2k.jpg" },
		{ .biome = Tundra,     .cacheName = "tundra",     .diffSrc = "Textures/Terrain/rocky_trail/rocky_trail_diff_2k.jpg",           .norSrc = "Textures/Terrain/rocky_trail/rocky_trail_nor_gl_2k.jpg",           .armSrc = "Textures/Terrain/rocky_trail/rocky_trail_arm_2k.jpg" },
		{ .biome = Arctic,     .cacheName = "arctic",     .diffSrc = "Textures/Terrain/snow_02/snow_02_diff_2k.jpg",                   .norSrc = "Textures/Terrain/snow_02/snow_02_nor_gl_2k.jpg",                   .armSrc = "Textures/Terrain/snow_02/snow_02_arm_2k.jpg" },
		{ .biome = Lakes,      .cacheName = "lakes",      .diffSrc = "Textures/Terrain/brown_mud_03/brown_mud_03_diff_2k.jpg",         .norSrc = "Textures/Terrain/brown_mud_03/brown_mud_03_nor_gl_2k.jpg",         .armSrc = "Textures/Terrain/brown_mud_03/brown_mud_03_arm_2k.jpg" },
		{ .biome = Highlands,  .cacheName = "highlands",  .diffSrc = "Textures/Terrain/rock_face/rock_face_diff_2k.jpg",               .norSrc = "Textures/Terrain/rock_face/rock_face_nor_gl_2k.jpg",               .armSrc = "Textures/Terrain/rock_face/rock_face_arm_2k.jpg" },
		// Rock layer: 6 climate-picked types spanning (temperature, precipitation) so mountains/crags read
		// differently by biome instead of one rock texture everywhere. Same REAL units as GeneratorV2's
		// BIOME_TABLE; keep them aimed at the ground attractors they are meant to accompany.
		{ .kind = ESourceKind::Rock, .rockTempC = -12.0f, .rockPrecipMm = 770.0f, .cacheName = "rock_gray",  .diffSrc = "Textures/Terrain/gray_rocks/gray_rocks_diff_2k.jpg",                     .norSrc = "Textures/Terrain/gray_rocks/gray_rocks_nor_gl_2k.jpg",                     .armSrc = "Textures/Terrain/gray_rocks/gray_rocks_arm_2k.jpg" }, // cold/dry granite: tundra, highlands, arctic peaks
		{ .kind = ESourceKind::Rock, .rockTempC = 24.0f,  .rockPrecipMm = 220.0f, .cacheName = "rock_red",   .diffSrc = "Textures/Terrain/terrain_red_01/terrain_red_01_diff_2k.jpg",             .norSrc = "Textures/Terrain/terrain_red_01/terrain_red_01_nor_gl_2k.jpg",             .armSrc = "Textures/Terrain/terrain_red_01/terrain_red_01_arm_2k.jpg" }, // hot/dry sandstone: desert, savanna canyons
		{ .kind = ESourceKind::Rock, .rockTempC = 22.0f,  .rockPrecipMm = 1870.0f, .cacheName = "rock_dark",  .diffSrc = "Textures/Terrain/dark_rock/dark_rock_diff_2k.jpg",                       .norSrc = "Textures/Terrain/dark_rock/dark_rock_nor_gl_2k.jpg",                       .armSrc = "Textures/Terrain/dark_rock/dark_rock_arm_2k.jpg" },       // warm/wet dark basalt: rainforest, swampland cliffs
		{ .kind = ESourceKind::Rock, .rockTempC = 5.0f,   .rockPrecipMm = 1650.0f, .cacheName = "rock_mossy", .diffSrc = "Textures/Terrain/rock_pitted_mossy/rock_pitted_mossy_diff_2k.jpg",       .norSrc = "Textures/Terrain/rock_pitted_mossy/rock_pitted_mossy_nor_gl_2k.jpg",       .armSrc = "Textures/Terrain/rock_pitted_mossy/rock_pitted_mossy_arm_2k.jpg" }, // cool/wet lichened rock: forest, taiga
		{ .kind = ESourceKind::Rock, .rockTempC = 17.0f,  .rockPrecipMm = 770.0f, .cacheName = "rock_worn",  .diffSrc = "Textures/Terrain/worn_rock_natural_01/worn_rock_natural_01_diff_2k.jpg", .norSrc = "Textures/Terrain/worn_rock_natural_01/worn_rock_natural_01_nor_gl_2k.jpg", .armSrc = "Textures/Terrain/worn_rock_natural_01/worn_rock_natural_01_arm_2k.jpg" }, // warm/dry weathered sandstone: savanna, highlands
		{ .kind = ESourceKind::Rock, .rockTempC = 10.0f,  .rockPrecipMm = 1210.0f, .cacheName = "rock_coast", .diffSrc = "Textures/Terrain/rock_3/rock_3_diff_2k.jpg",                             .norSrc = "Textures/Terrain/rock_3/rock_3_nor_gl_2k.jpg",                             .armSrc = "Textures/Terrain/rock_3/rock_3_arm_2k.jpg" }, // temperate coastal rock: grassland, lakes cliffs
		// Shoreline overlay: NOT a biome, always the trailing material (Renderer::setTerrainBiomeMaterials).
		{ .kind = ESourceKind::Beach, .cacheName = "beach", .diffSrc = "Textures/Terrain/coast_sand_01/coast_sand_01_diff_2k.jpg", .norSrc = "Textures/Terrain/coast_sand_01/coast_sand_01_nor_gl_2k.jpg", .armSrc = "Textures/Terrain/coast_sand_01/coast_sand_01_arm_2k.jpg" },
	};

	constexpr const char* TERRAIN_TEX_CACHE_DIR = "Local/TerrainTex/";

	std::string terrainTexCachePath(const TerrainTexSource& src, const char* map)
	{
		return std::string(TERRAIN_TEX_CACHE_DIR) + src.cacheName + "_" + map + ".dds";
	}

	// True when outPath exists and is newer than every present source (missing sources don't invalidate).
	bool terrainTexCacheFresh(const std::string& outPath, std::initializer_list<const char*> sources)
	{
		std::error_code ec;
		const auto outTime = std::filesystem::last_write_time(outPath, ec);
		if (ec)
			return false;
		for (const char* src : sources)
		{
			if (!src)
				continue;
			const auto srcTime = std::filesystem::last_write_time(src, ec);
			if (!ec && srcTime > outTime)
				return false;
		}
		return true;
	}

	// Bakes every stale terrain texture into the DDS cache. Runs on a background jthread at startup;
	// one conversion is seconds of CPU (mips + BC compression), so stale entries fan out over a small
	// thread pool and the stop token is checked between files (app shutdown mid-first-bake).
	void bakeTerrainTexCache(std::stop_token stopToken)
	{
		std::error_code ec;
		std::filesystem::create_directories(TERRAIN_TEX_CACHE_DIR, ec);

		struct BakeTask { const TerrainTexSource* src; int map; }; // map: 0 = diff, 1 = nor, 2 = arm
		std::vector<BakeTask> tasks;
		for (const TerrainTexSource& src : TERRAIN_TEX_SOURCES)
		{
			if (!terrainTexCacheFresh(terrainTexCachePath(src, "diff"), { src.diffSrc }))
				tasks.push_back({ &src, 0 });
			if (!terrainTexCacheFresh(terrainTexCachePath(src, "nor"), { src.norSrc }))
				tasks.push_back({ &src, 1 });
			if (!terrainTexCacheFresh(terrainTexCachePath(src, "arm"), { src.armSrc, src.aoSrc, src.roughSrc }))
				tasks.push_back({ &src, 2 });
		}
		if (tasks.empty())
			return;

		const auto bakeStart = std::chrono::steady_clock::now();
		std::atomic<size_t> nextTask{ 0 };
		const uint32 numThreads = glm::clamp(std::thread::hardware_concurrency(), 2u, (uint32)tasks.size());
		std::vector<std::thread> pool;
		pool.reserve(numThreads);
		for (uint32 t = 0; t < numThreads; ++t)
		{
			pool.emplace_back([&]()
			{
				for (size_t i = nextTask.fetch_add(1); i < tasks.size(); i = nextTask.fetch_add(1))
				{
					if (stopToken.stop_requested())
						return;
					const TerrainTexSource& src = *tasks[i].src;
					bool ok = false;
					switch (tasks[i].map)
					{
					case 0: ok = TextureConvert::convertToDds(src.diffSrc, TextureConvert::EUsage::Color, terrainTexCachePath(src, "diff").c_str()); break;
					case 1: ok = TextureConvert::convertToDds(src.norSrc, TextureConvert::EUsage::NormalMap, terrainTexCachePath(src, "nor").c_str()); break;
					case 2:
						ok = src.armSrc
							? TextureConvert::convertToDds(src.armSrc, TextureConvert::EUsage::Data, terrainTexCachePath(src, "arm").c_str())
							: TextureConvert::convertPackedToDds(src.aoSrc, src.roughSrc, nullptr, terrainTexCachePath(src, "arm").c_str());
						break;
					}
					if (!ok)
						Log::warning(std::format("Terrain: failed to bake splat texture '{}' map {}", src.cacheName, tasks[i].map));
				}
			});
		}
		for (std::thread& t : pool)
			t.join();
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bakeStart).count();
		Log::info(std::format("Terrain: baked {} splat textures into {} in {} ms", tasks.size(), TERRAIN_TEX_CACHE_DIR, ms));
	}
}

namespace Procedural
{
	TerrainStreamer::~TerrainStreamer()
	{
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			m_running = false;
		}
		m_cv.notify_all();
		if (m_worker.joinable())
			m_worker.join();
		clearResidents(); // main thread: free RenderNodes/containers while the renderer is still alive
	}

	void TerrainStreamer::initialize()
	{
		auto dirty = [this]() { m_configDirty = true; };

		Tweak::boolean("Terrain", "Enabled", &m_enabled);
		// V2 = noise field, evaluated per point. V3 = Terrain Diffusion (ONNX): needs 2.28 GB of models
		// (downloaded on first selection) and a DirectML GPU; terrain appears once they finish loading.
		static constexpr std::string_view generatorNames[] = { "V2 (noise)", "V3 (diffusion)" };
		Tweak::enumVar("Terrain", "Generator", &m_generator, generatorNames, dirty);
		Tweak::intVar("Terrain", "Seed", &m_seed, 0, 1000000, 1.0f, dirty);
		Tweak::intVar("Terrain", "Chunk size (m)", &m_chunkSize, 128, 1024, 1.0f, dirty);
		Tweak::intVar("Terrain", "LOD0 resolution", &m_lod0Res, 4, 512, 1.0f, dirty);
		Tweak::intVar("Terrain", "Range (chunks)", &m_ringRadius, 1, 64, 1.0f); // max generation radius from the camera chunk
		Tweak::floatVar("Terrain", "LOD step (chunks)", &m_lodStep, 0.1f, 4.0f, 0.1f); // LOD0 band width; each next band doubles
		Tweak::intVar("Terrain", "Max LOD", &m_maxLod, 0, 6, 1.0f);
		Tweak::intVar("Terrain", "Uploads/frame", &m_maxUploadsPerFrame, 1, 32, 1.0f);
		Tweak::floatVar("Terrain", "Sea level (m)", &m_seaLevel, -200.0f, 200.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain", "Skirt depth (m)", &m_skirtDepth, 0.0f, 64.0f, 0.5f, dirty);
		// ONE shared baked terrain-data map (height, water level, fog|falloff|temp|hum, altitude): the volumetric
		// fog's terrain follow + regional thickness, the ocean's far shore fallback, and the terrain
		// coloring all read these cascades. Disabling it degrades all three.
		Tweak::boolean("Terrain", "Terrain data map", &m_terrainMapEnabled);
		// Diagnostic: prints what each baked cascade actually contains, decoded the way the shader
		// decodes it. The bake is otherwise invisible — a wrong sampler, pack or upload all look
		// the same from the shader side.
		Tweak::boolean("Terrain", "Log baked data map", &m_terrainMapDebugLog);
		Tweak::floatVar("Terrain", "Data map range (m)", &m_terrainMapRange, 256.0f, 8192.0f, 32.0f);
		// Floor for the far cascade world size: the actual range is raised to cover the resident mesh ring
		// (2*(R+1)*chunkSize) so distant terrain never reads clamp-to-edge frozen altitude/temperature.
		Tweak::floatVar("Terrain", "Data map far range (m)", &m_terrainMapFarRange, 1024.0f, 65536.0f, 256.0f);

		// Terrain biome texture splatting (TERRAIN pipeline variant; pushed to the renderer every frame
		// from updateTerrainTextures via Renderer::setTerrainTextureParams).
		Tweak::floatVar("Terrain/Textures", "Ground uv scale (1/m)", &m_texUvScaleGround, 0.005f, 2.0f);
		Tweak::floatVar("Terrain/Textures", "Rock uv scale (1/m)", &m_texUvScaleRock, 0.005f, 2.0f);
		Tweak::floatVar("Terrain/Textures", "Slope rock start", &m_texSlopeRockStart, 0.0f, 1.0f);
		Tweak::floatVar("Terrain/Textures", "Slope rock full", &m_texSlopeRockFull, 0.0f, 1.0f);
		Tweak::floatVar("Terrain/Textures", "Crag relief start (m)", &m_texCragStart, 0.0f, 200.0f);
		Tweak::floatVar("Terrain/Textures", "Crag relief full (m)", &m_texCragFull, 0.0f, 400.0f);
		Tweak::floatVar("Terrain/Textures", "Beach band (m)", &m_texBeachBand, 0.0f, 20.0f);
		Tweak::floatVar("Terrain/Textures", "Blend width scale", &m_texBlendScale, 0.25f, 6.0f);

		Tweak::floatVar("Terrain/V2", "Biome size (m)", &m_v2BiomeSize, 8.0f, 8000.0f, 10.0f, dirty);  // climate-field feature size
		Tweak::floatVar("Terrain/V2", "Biome blending", &m_v2BiomeBlend, 0.1f, 3.0f, 0.01f, dirty);   // climate-space kernel width (low = crisp biomes)
		Tweak::floatVar("Terrain/V2", "Border warp (m)", &m_v2BorderWarp, 0.0f, 1500.0f, 5.0f, dirty);
		Tweak::floatVar("Terrain/V2", "Ocean fraction", &m_v2OceanFraction, 0.0f, 1.0f, 0.01f, dirty);
		Tweak::floatVar("Terrain/V2", "Continent freq", &m_v2ContinentFreq, 0.0f, FLT_MAX, 0.00001f, dirty); // lower = bigger oceans/continents
		Tweak::floatVar("Terrain/V2", "Inland rise (m)", &m_v2InlandRise, 0.0f, 2500.0f, 1.0f, dirty);        // coast -> deep inland altitude gain
		Tweak::floatVar("Terrain/V2", "Ocean deepen (m)", &m_v2OceanDeepen, 0.0f, 5000.0f, 1.0f, dirty);
		Tweak::floatVar("Terrain/V2", "Temp lapse", &m_v2TempLapse, 0.0f, 0.01f, 0.0001f, dirty);            // altitude -> cold
		Tweak::floatVar("Terrain/V2", "Climate bands", &m_v2ClimateBandAmp, 0.0f, 0.6f, 0.01f, dirty);       // regional hot/cold swing
		Tweak::floatVar("Terrain/V2", "Height scale", &m_v2HeightScale, 0.0f, 8.0f, 0.05f, dirty);
		Tweak::floatVar("Terrain/V2", "Altitude texel (m)", &m_v2AltitudeTexel, 8.0f, 512.0f, 1.0f, dirty); // macro elevation lattice
		Tweak::floatVar("Terrain/V2", "Altitude amp (m)", &m_v2AltitudeAmp, 0.0f, 400.0f, 1.0f, dirty);     // large-scale relief
		Tweak::floatVar("Terrain/V2", "Altitude freq", &m_v2AltitudeFreq, 0.0f, FLT_MAX, 0.00005f, dirty);
		Tweak::floatVar("Terrain/V2", "Peak amp (m)", &m_v2PeakAmp, 0.0f, 1000.0f, 5.0f, dirty);       // fantasy ranges
		Tweak::floatVar("Terrain/V2", "Peak threshold", &m_v2PeakThreshold, 0.0f, 1.0f, 0.01f, dirty); // higher = rarer
		Tweak::floatVar("Terrain/V2", "Peak sharpness", &m_v2PeakSharpness, 0.1f, 6.0f, 0.05f, dirty);
		Tweak::floatVar("Terrain/V2", "Peak freq", &m_v2PeakFreq, 0.0f, FLT_MAX, 0.00002f, dirty);
		Tweak::floatVar("Terrain/V2", "Mtn detail amp (m)", &m_v2MtnDetailAmp, 0.0f, 300.0f, 1.0f, dirty); // craggy ridges on high ground
		Tweak::floatVar("Terrain/V2", "Mtn detail freq", &m_v2MtnDetailFreq, 0.0f, FLT_MAX, 0.0005f, dirty);
		Tweak::floatVar("Terrain/V2", "Mtn mask start (m)", &m_v2MtnMaskStart, 0.0f, 300.0f, 1.0f, dirty); // MACRO RELIEF above the local baseline, not raw altitude
		Tweak::floatVar("Terrain/V2", "Mtn mask full (m)", &m_v2MtnMaskFull, 0.0f, 500.0f, 1.0f, dirty);
		Tweak::floatVar("Terrain/V2", "Grass fog", &m_v2GrassFog, 0.0f, 1.0f, 0.01f, dirty);   // patchy ground mist
		Tweak::floatVar("Terrain/V2", "Valley fog", &m_v2ValleyFog, 0.0f, 1.0f, 0.01f, dirty); // fills mountain valleys
		Tweak::floatVar("Terrain/V2", "Detail freq", &m_detailFrequency, 0.0f, FLT_MAX, 0.001f, dirty); // height detail fBm
		Tweak::intVar("Terrain/V2", "Detail octaves", &m_detailOctaves, 1, 8, 1.0f, dirty);

		// V3 (Terrain Diffusion). The model resolves 30 m/px, which is what makes its continents
		// continent-sized; everything below that is the slope-masked detail layer below.
		// "Meters per pixel" is a UNIFORM world scale: heights and detail shrink with it, so the model's
		// proportions survive and "Height scale" stays a pure exaggeration on top (1 = real proportions).
		// NOTE ON COST: lowering it compresses the world (mountains become reachable sooner) but is
		// quadratically MORE expensive — the same view distance then spans more model pixels, so more tiles
		// must be generated. 30 -> 15 is ~4x the inference work for the same ring radius.
		Tweak::floatVar("Terrain/V3", "Meters per pixel", &m_v3MetersPerPixel, 1.0f, 60.0f, 0.5f, dirty); // 30 = true scale
		Tweak::floatVar("Terrain/V3", "Height scale", &m_v3HeightScale, 0.0f, 4.0f, 0.05f, dirty);
		// The model's climate is real-world calibrated and the biome attractors are expressed in real units
		// to match, so both of these default to 0 (= the model's own climate). "Extra lapse" pushes the
		// snow line further down the mountains than reality; "Temp offset" makes the whole planet warmer or
		// colder.
		Tweak::floatVar("Terrain/V3", "Temp offset (C)", &m_v3TemperatureOffset, -30.0f, 30.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain/V3", "Extra lapse (C/m)", &m_v3ExtraLapseRate, 0.0f, 0.02f, 0.0005f, dirty);
		Tweak::floatVar("Terrain/V3", "Detail slope gain", &m_v3DetailSlopeGain, 0.0f, 4.0f, 0.05f, dirty); // higher = detail on gentler slopes
		Tweak::floatVar("Terrain/V3", "Detail A wavelength (m)", &m_v3DetailWavelengthA, 20.0f, 1000.0f, 5.0f, dirty);
		Tweak::floatVar("Terrain/V3", "Detail A amp (m)", &m_v3DetailAmplitudeA, 0.0f, 200.0f, 1.0f, dirty);
		Tweak::floatVar("Terrain/V3", "Detail B wavelength (m)", &m_v3DetailWavelengthB, 5.0f, 300.0f, 1.0f, dirty);
		Tweak::floatVar("Terrain/V3", "Detail B amp (m)", &m_v3DetailAmplitudeB, 0.0f, 80.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain/V3", "Precip for full humidity", &m_v3PrecipFullHumidity, 200.0f, 6000.0f, 50.0f, dirty);
		Tweak::floatVar("Terrain/V3", "Humidity offset", &m_v3HumidityOffset, -1.0f, 1.0f, 0.01f, dirty);
		Tweak::floatVar("Terrain/V3", "Humid fog", &m_v3HumidFog, 0.0f, 1.0f, 0.01f, dirty);
		Tweak::floatVar("Terrain/V3", "Valley fog", &m_v3ValleyFog, 0.0f, 1.0f, 0.01f, dirty);
		Tweak::intVar("Terrain/V3", "Resident tiles", &m_v3MaxTiles, 4, 256, 1.0f, dirty); // ~800 KB each

		rebuildMaps();

		// Bake the biome splat textures to the DDS cache in the background (no-op when fresh); chunks
		// render with the flat-color fallback until updateTerrainTextures registers the finished set.
		m_texBakeWorker = std::jthread([this](std::stop_token stopToken)
		{
			bakeTerrainTexCache(stopToken);
			m_texBakeDone.store(!stopToken.stop_requested(), std::memory_order_release);
		});

		m_running = true;
		m_worker = std::thread([this]() { workerLoop(); });
	}

	void TerrainStreamer::updateTerrainTextures(Renderer& renderer)
	{
		// The generator clamps its kernel width the same way (TerrainGenV2 ctor); keep the shader's
		// climate blend live against the tweak without re-uploading the texture set.
		const float climateSigma = 0.10f * glm::clamp(m_v2BiomeBlend, 0.1f, 3.0f);
		renderer.setTerrainClimateSigma(climateSigma);

		renderer.setTerrainTextureParams({
			.uvScaleGround = m_texUvScaleGround,
			.uvScaleRock = m_texUvScaleRock,
			.slopeRockStart = m_texSlopeRockStart,
			.slopeRockFull = m_texSlopeRockFull,
			.cragStart = m_texCragStart,
			.cragFull = m_texCragFull,
			.beachBand = m_texBeachBand,
			.blendScale = m_texBlendScale,
		});

		if (m_texSetRegistered || !m_texBakeDone.load(std::memory_order_acquire))
			return;
		m_texSetRegistered = true;

		// Renderer::setTerrainBiomeMaterials requires [ground][rock][beach] contiguous, in that order —
		// build it in three passes over the table rather than relying on TERRAIN_TEX_SOURCES' declaration
		// order, so entries can be listed/grouped however is clearest there.
		auto tryBuildMat = [](const TerrainTexSource& src) -> std::optional<Renderer::TerrainBiomeMaterial>
		{
			Renderer::TerrainBiomeMaterial mat;
			mat.climateCoords = src.kind == ESourceKind::Ground
				? biomeClimateCoords(src.biome)
				: glm::vec2(temperatureTo01(src.rockTempC), precipTo01(src.rockPrecipMm));
			mat.diffuseDds = terrainTexCachePath(src, "diff");
			mat.normalDds = terrainTexCachePath(src, "nor");
			mat.armDds = terrainTexCachePath(src, "arm");
			std::error_code ec;
			if (!std::filesystem::exists(mat.diffuseDds, ec) || !std::filesystem::exists(mat.normalDds, ec) || !std::filesystem::exists(mat.armDds, ec))
			{
				// Source images missing/bake failed: drop the entry (ground/rock lose the attractor; a
				// missing beach entry just disables the shoreline overlay).
				Log::warning(std::format("Terrain: splat set '{}' incomplete, skipping", src.cacheName));
				return std::nullopt;
			}
			return mat;
		};

		std::vector<Renderer::TerrainBiomeMaterial> mats;
		uint32 numGround = 0, numRock = 0;
		for (const TerrainTexSource& src : TERRAIN_TEX_SOURCES)
			if (src.kind == ESourceKind::Ground)
				if (auto mat = tryBuildMat(src)) { mats.push_back(std::move(*mat)); numGround++; }
		for (const TerrainTexSource& src : TERRAIN_TEX_SOURCES)
			if (src.kind == ESourceKind::Rock)
				if (auto mat = tryBuildMat(src)) { mats.push_back(std::move(*mat)); numRock++; }
		for (const TerrainTexSource& src : TERRAIN_TEX_SOURCES)
			if (src.kind == ESourceKind::Beach)
				if (auto mat = tryBuildMat(src)) mats.push_back(std::move(*mat)); // at most one

		if (!mats.empty())
			renderer.setTerrainBiomeMaterials(mats, numGround, numRock, climateSigma);
	}

	void TerrainStreamer::rebuildMaps()
	{
		if (m_generator == 1)
		{
			// V3 can't sample anything until its models are resident. Constructing the generator is what
			// kicks the download/load off, so do that unconditionally, but only PUBLISH it once ready —
			// otherwise every chunk built in the meantime would bake a flat sea-level world into the
			// resident cache and never be revisited.
			TerrainConfigV3 cfg;
			cfg.seed = (uint32)m_seed;
			cfg.seaLevel = m_seaLevel;
			cfg.metersPerPixel = m_v3MetersPerPixel;
			cfg.heightScale = m_v3HeightScale;
			cfg.detailSlopeGain = m_v3DetailSlopeGain;
			cfg.detailWavelengthA = m_v3DetailWavelengthA;
			cfg.detailAmplitudeA = m_v3DetailAmplitudeA;
			cfg.detailWavelengthB = m_v3DetailWavelengthB;
			cfg.detailAmplitudeB = m_v3DetailAmplitudeB;
			cfg.precipForFullHumidity = m_v3PrecipFullHumidity;
			cfg.humidityOffset = m_v3HumidityOffset;
			cfg.temperatureOffset = m_v3TemperatureOffset;
			cfg.extraLapseRate = m_v3ExtraLapseRate;
			cfg.humidFogAmount = m_v3HumidFog;
			cfg.valleyFogAmount = m_v3ValleyFog;
			cfg.maxResidentTiles = m_v3MaxTiles;

			std::shared_ptr<const ITerrainSampler> maps;
			if (TerrainGenV3::isReady())
			{
				maps = std::make_shared<const TerrainGenV3>(cfg);
				m_v3AwaitingModels = false;
			}
			else
			{
				// Cheap: the ctor only registers interest and starts the background load.
				const TerrainGenV3 kick(cfg);
				m_v3AwaitingModels = !TerrainGenV3::hasFailed();
				if (TerrainGenV3::hasFailed())
					Log::error("[Terrain] V3 unavailable (model load failed) - staying on an empty world; "
					           "switch Terrain/Generator back to V2");
			}

			std::lock_guard<std::mutex> lk(m_mutex);
			m_maps = std::move(maps);
			++m_generation;
			m_requests.clear();
			return;
		}

		m_v3AwaitingModels = false;
		TerrainConfigV2 cfg;
		cfg.seed = (uint32)m_seed;
		cfg.seaLevel = m_seaLevel;
		cfg.biomeSize = m_v2BiomeSize;
		cfg.biomeBlend = m_v2BiomeBlend;
		cfg.borderWarp = m_v2BorderWarp;
		cfg.oceanFraction = m_v2OceanFraction;
		cfg.continentFrequency = m_v2ContinentFreq;
		cfg.inlandRise = m_v2InlandRise;
		cfg.oceanDeepen = m_v2OceanDeepen;
		cfg.temperatureLapse = m_v2TempLapse;
		cfg.climateBandAmplitude = m_v2ClimateBandAmp;
		cfg.heightScale = m_v2HeightScale;
		cfg.altitudeTexelSize = m_v2AltitudeTexel;
		cfg.altitudeAmplitude = m_v2AltitudeAmp;
		cfg.altitudeFrequency = m_v2AltitudeFreq;
		cfg.peakAmplitude = m_v2PeakAmp;
		cfg.peakThreshold = m_v2PeakThreshold;
		cfg.peakSharpness = m_v2PeakSharpness;
		cfg.peakFrequency = m_v2PeakFreq;
		cfg.mountainDetailAmplitude = m_v2MtnDetailAmp;
		cfg.mountainDetailFrequency = m_v2MtnDetailFreq;
		cfg.mountainMaskStart = m_v2MtnMaskStart;
		cfg.mountainMaskFull = m_v2MtnMaskFull;
		cfg.detailFrequency = m_detailFrequency;
		cfg.detailOctaves = (uint32)glm::max(1, m_detailOctaves);
		cfg.grassFogAmount = m_v2GrassFog;
		cfg.valleyFogAmount = m_v2ValleyFog;
		std::shared_ptr<const ITerrainSampler> maps = std::make_shared<const TerrainGenV2>(cfg);

		std::lock_guard<std::mutex> lk(m_mutex);
		m_maps = std::move(maps);
		++m_generation;
		m_requests.clear(); // drop queued work built against the old config
	}

	std::shared_ptr<const ITerrainSampler> TerrainStreamer::currentMaps()
	{
		std::lock_guard<std::mutex> lk(m_mutex);
		return m_maps;
	}

	void TerrainStreamer::clearResidents()
	{
		m_residents.clear();
		m_pending.clear();
		m_readyBacklog.clear();
	}

	void TerrainStreamer::workerLoop()
	{
		for (;;)
		{
			Request req;
			bool haveWork = false;
			{
				std::unique_lock<std::mutex> lk(m_mutex);
				m_cv.wait(lk, [this]() { return !m_running || !m_requests.empty(); });
				if (!m_running)
					return;
				// Lazy staleness: requests that fell out of the ring while queued are dropped HERE, at
				// dequeue, against the published ring state — the main thread never rewrites the queue.
				// A dropped request bounces back as an EMPTY result so the main thread releases its
				// pending key (m_pending is main-thread-owned). Skipping costs a few integer ops per
				// entry, so a deep stale backlog from fast flight burns off in microseconds.
				while (!m_requests.empty())
				{
					req = std::move(m_requests.front());
					m_requests.pop_front();
					const glm::ivec2 d = req.params.coord - m_ringCam;
					const int cheb = glm::max(glm::abs(d.x), glm::abs(d.y));
					if (cheb <= m_ringR && req.params.lod == ringLodAt(cheb, m_ringLodStep, m_ringMaxLod))
					{
						haveWork = true;
						break;
					}
					Result drop;
					drop.key = req.key;
					drop.generation = req.generation;
					drop.coord = req.params.coord;
					drop.lod = req.params.lod;
					m_results.push_back(std::move(drop)); // empty mesh = dropped
				}
			}
			if (!haveWork)
				continue; // everything was stale: back to waiting

			Result res;
			res.key = req.key;
			res.generation = req.generation;
			res.coord = req.params.coord;
			res.lod = req.params.lod;
			if (req.maps)
				generateChunk(*req.maps, req.params, res.mesh);

			{
				std::lock_guard<std::mutex> lk(m_mutex);
				m_results.push_back(std::move(res));
			}
		}
	}

	// Shared terrain-data map: FOG_TERRAIN_CASCADES camera-centered snapshots (HeightMapBaker, sampled
	// from the SAME sampler the chunks render from) — a near cascade over m_terrainMapRange meters at
	// fine texels and a far cascade over m_terrainMapFarRange at the same resolution (coarse texels are
	// fine at those distances, so long-range data costs no extra memory). Per texel: height, water level,
	// packed fog|falloff|temp|hum, macro altitude. Consumers: the volumetric fog's terrain follow + regional
	// thickness, the ocean's shore-map fallback, and the TERRAIN pipeline's coloring. (The renderer-side
	// API keeps the historical "FogTerrainHeightMap" name — fog was its first consumer.)
	void TerrainStreamer::updateFogHeightMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& maps, float farRange)
	{
		const bool active = m_terrainMapEnabled && maps != nullptr;
		HeightMapBaker::Baked baked;
		if (m_terrainMapBaker.update(baked, active, maps, glm::vec2(camera.position.x, camera.position.z),
			glm::vec2(glm::max(m_terrainMapRange, 256.0f), farRange),
			RendererVKLayout::FOG_TERRAIN_RES, RendererVKLayout::FOG_TERRAIN_CASCADES, 4)) // RGBA: height, water level, fog|falloff|temp|hum, altitude
		{
			// Decode what actually went into the packed climate channel, per cascade, exactly as
			// terrain_height.inc.glsl does. The bake is the one link in the chain nothing else can see: a
			// wrong sampler, a wrong pack and a wrong upload all look identical from the shader.
			if (m_terrainMapDebugLog)
			{
				const uint32 res = RendererVKLayout::FOG_TERRAIN_RES;
				const size_t perCascade = (size_t)res * res * 4;
				for (uint32 c = 0; c < RendererVKLayout::FOG_TERRAIN_CASCADES; c++)
				{
					float tMin = FLT_MAX, tMax = -FLT_MAX, hMin = FLT_MAX, hMax = -FLT_MAX;
					float yMin = FLT_MAX, yMax = -FLT_MAX, aMin = FLT_MAX, aMax = -FLT_MAX;
					for (size_t i = 0; i < perCascade; i += 4)
					{
						const float* t = baked.texels.data() + (size_t)c * perCascade + i;
						const uint32 packed = std::bit_cast<uint32>(t[2]);
						const float temp = float((packed >> 16) & 255u) * (75.0f / 255.0f) - 25.0f;
						const float hum = float((packed >> 24) & 255u) * (1.0f / 255.0f);
						tMin = glm::min(tMin, temp); tMax = glm::max(tMax, temp);
						hMin = glm::min(hMin, hum);  hMax = glm::max(hMax, hum);
						yMin = glm::min(yMin, t[0]); yMax = glm::max(yMax, t[0]);
						aMin = glm::min(aMin, t[3]); aMax = glm::max(aMax, t[3]);
					}
					Log::info(std::format("[Terrain] baked cascade {} ({:.0f} m): height {:.0f}..{:.0f} | "
					                      "altitude {:.0f}..{:.0f} | temp {:.1f}..{:.1f} C | humidity {:.2f}..{:.2f}",
					                      c, baked.ranges[(int)c], yMin, yMax, aMin, aMax, tMin, tMax, hMin, hMax));
				}
			}
			renderer.setFogTerrainHeightMap(baked.texels, baked.center, baked.ranges, maps->seaLevel());
			m_terrainMapUploaded = true;
		}
		if (!active && m_terrainMapUploaded)
		{
			renderer.clearFogTerrainHeightMap(); // fog reverts to the flat base; ocean/coloring lose their data
			m_terrainMapUploaded = false;
		}
	}

	void TerrainStreamer::update(Renderer& renderer, const Camera& camera)
	{
		//m_terrainMapFarRange = camera.far;
		if (m_configDirty)
		{
			m_configDirty = false;
			rebuildMaps();
			clearResidents(); // geometry/climate changed: regenerate everything against the new config
		}

		// V3's 2.28 GB of models load on a background thread; poll for the handover. Until then m_maps is
		// null and nothing is generated, so the world stays empty rather than flat.
		if (m_v3AwaitingModels)
		{
			if (TerrainGenV3::isReady() || TerrainGenV3::hasFailed())
			{
				rebuildMaps();
				clearResidents();
			}
			else
			{
				// Rate-limited: loading 2.28 GB onto the GPU takes a few seconds.
				const double now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
				if (now - m_v3LastStatusLog > 1.0)
				{
					m_v3LastStatusLog = now;
					Log::info(std::format("[Terrain] V3: {}", TerrainGenV3::statusText()));
				}
			}
		}

		if (!m_enabled)
		{
			if (!m_residents.empty() || !m_pending.empty())
				clearResidents();
			updateFogHeightMap(renderer, camera, nullptr, m_terrainMapFarRange); // no terrain -> no terrain-following fog
			renderer.setTerrainParams(0.0f, m_seaLevel);   // no mesh up: disables the ocean land cull
			return;
		}

		updateTerrainTextures(renderer);

		const float chunkSize = (float)m_chunkSize;
		const int camCX = (int)std::floor(camera.position.x / chunkSize);
		const int camCZ = (int)std::floor(camera.position.z / chunkSize);
		const int R = glm::max(1, m_ringRadius);
		const float lodStep = glm::max(0.01f, m_lodStep);
		const uint32 maxLod = (uint32)glm::max(0, m_maxLod);

		// Chunk mesh coverage: chunks span +-R around the camera's chunk, so a disk of R*chunkSize around
		// the camera is guaranteed resident whatever its position within its own chunk — the fence for
		// the ocean's buried-under-land vertex cull. Also carries the live sea level (terrain coloring).
		renderer.setTerrainParams((float)R * chunkSize, m_seaLevel);

		std::shared_ptr<const ITerrainSampler> maps;
		uint32 generation;
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			maps = m_maps;
			generation = m_generation;
		}

		// The far cascade must cover the whole resident mesh, else terrain past its edge reads clamp-to-edge
		// (frozen altitude/temperature -> distant inland looks uniformly cold/snowy, ignoring the inland
		// rise). The mesh reaches ~(R+1)*chunkSize from the camera on each axis; the map is a centered
		// square of side = range, so range must be at least twice that. Keep the tweak as a floor.
		const float meshHalfExtent = (float)(R + 1) * chunkSize;
		const float farRange = glm::max(m_terrainMapFarRange, 2.0f * meshHalfExtent);
		updateFogHeightMap(renderer, camera, maps, farRange);

		// Ring membership is CLOSED FORM: the column at Chebyshev distance cheb from the camera chunk
		// wants ringLodAt(cheb) (geometric bands), nothing outside R. Every "is this still wanted"
		// question below (result validation, eviction) is this arithmetic — no desired-key sets.
		const auto ringLod = [&](glm::ivec2 coord) -> int
		{
			const int cheb = glm::max(glm::abs(coord.x - camCX), glm::abs(coord.y - camCZ));
			return cheb <= R ? (int)ringLodAt(cheb, lodStep, maxLod) : -1;
		};

		// --- Enqueue any ring chunk that is neither resident nor already in flight.
		std::vector<Request> newRequests;
		for (int dz = -R; dz <= R; ++dz)
		{
			for (int dx = -R; dx <= R; ++dx)
			{
				const int cheb = glm::max(dx < 0 ? -dx : dx, dz < 0 ? -dz : dz);
				const uint32 lod = ringLodAt(cheb, lodStep, maxLod);
				const glm::ivec2 coord(camCX + dx, camCZ + dz);
				const uint64 key = chunkKey(coord, lod);
				if (m_residents.count(key) || m_pending.count(key))
					continue;

				Request req;
				req.key = key;
				req.generation = generation;
				req.maps = maps;
				req.params.coord = coord;
				req.params.lod = lod;
				req.params.chunkSize = chunkSize;
				req.params.lod0Res = (uint32)glm::max(1, m_lod0Res);
				req.params.skirtDepth = m_skirtDepth;
				newRequests.push_back(std::move(req));
				m_pending.insert(key);
			}
		}

		// Out-of-range queued work is dropped LAZILY by the worker at dequeue (see workerLoop) — the main
		// thread never prunes or re-sorts the queue. It only publishes the ring state the worker
		// validates against, and appends new work nearest-first: each batch is sorted (a batch is either
		// the initial/teleport fill, where nearest-first matters, or a thin leading-edge strip of roughly
		// equidistant chunks), and batches are consumed in enqueue order, so ground still materializes
		// under the camera outward during flight.
		const bool ringMoved = camCX != m_lastRingCX || camCZ != m_lastRingCZ
			|| R != m_lastRingR || lodStep != m_lastRingLodStep || maxLod != m_lastRingMaxLod;
		m_lastRingCX = camCX; m_lastRingCZ = camCZ;
		m_lastRingR = R; m_lastRingLodStep = lodStep; m_lastRingMaxLod = maxLod;
		if (ringMoved || !newRequests.empty())
		{
			const glm::ivec2 cam(camCX, camCZ);
			std::sort(newRequests.begin(), newRequests.end(), [&](const Request& a, const Request& b)
			{
				const glm::ivec2 da = a.params.coord - cam, db = b.params.coord - cam;
				return da.x * da.x + da.y * da.y < db.x * db.x + db.y * db.y;
			});
			std::lock_guard<std::mutex> lk(m_mutex);
			m_ringCam = cam;
			m_ringR = R;
			m_ringLodStep = lodStep;
			m_ringMaxLod = maxLod;
			for (Request& r : newRequests)
				m_requests.push_back(std::move(r));
		}
		if (!newRequests.empty())
			m_cv.notify_all();

		// --- Drain generated chunks (backlog first, then whatever the worker finished), budget-limited.
		std::vector<Result> ready = std::move(m_readyBacklog);
		m_readyBacklog.clear();
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			for (Result& r : m_results)
				ready.push_back(std::move(r));
			m_results.clear();
		}

		int uploads = 0;
		for (Result& res : ready)
		{
			if (res.mesh.indices.empty()) // worker-dropped (stale at dequeue): just release the key
			{
				m_pending.erase(res.key); // re-enters the ring as a fresh request if wanted again
				continue;
			}
			const bool valid = (res.generation == generation) && (int)res.lod == ringLod(res.coord) && !m_residents.count(res.key);
			if (!valid)
			{
				m_pending.erase(res.key); // stale / no longer wanted / duplicate: done with it
				continue;
			}
			if (uploads >= m_maxUploadsPerFrame)
			{
				m_readyBacklog.push_back(std::move(res)); // stays "pending" so it isn't re-requested
				continue;
			}

			MeshGeometryDesc geom;
			geom.positions = res.mesh.positions.data();
			geom.normals = res.mesh.normals.data();
			geom.tangents = res.mesh.tangents.data();
			geom.bitangents = res.mesh.bitangents.data();
			geom.texCoords = res.mesh.texCoords.data();
			geom.numVertices = (uint32)res.mesh.positions.size();
			geom.indices = res.mesh.indices.data();
			geom.numIndices = (uint32)res.mesh.indices.size();
			geom.name = "TerrainChunk";

			// Geometry only — no per-chunk texture; the material falls back to the shared renderer textures
			// (a dedicated terrain shader will color the surface from height/biome later).
			std::unique_ptr<ISceneData> scene = ISceneData::createMeshScene(geom);
			m_pending.erase(res.key);
			if (!scene)
				continue;

			// Route the chunk onto the terrain pipeline variant (procedural height/slope albedo). Keep the
			// material's own texture indices (fallback) — terrain carries no textures.
			ObjectContainer::MaterialOverrides overrides;
			overrides.pipelineIdx = RendererVKLayout::EPipelineIndex::TerrainLit;
			overrides.useSceneTextures = true;
			// Chunks are already LOD'd by the streamer (resolution picked per ring distance), so the
			// renderer's per-chunk meshopt LOD chains are pure redundant churn: 5x the MeshInfos plus LOD
			// groups and BLAS-alias sharing, all recreated on every re-LOD. Disable them so each chunk is a
			// single mesh with one identity-aliased BLAS — far less churn through the mesh/RT free lists.
			overrides.disableGeneratedLods = true;
			auto container = std::make_unique<ObjectContainer>();
			if (!container->initialize(*scene, &overrides))
				continue;

			const Transform transform(
				glm::vec3((float)res.coord.x * chunkSize, 0.0f, (float)res.coord.y * chunkSize),
				1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
			RenderNode node = container->spawnRootNode(transform);

			Resident resident;
			resident.container = std::move(container);
			resident.coord = res.coord;
			resident.lod = res.lod;
			resident.node = std::move(node);
			// Culling registration: chunks live in the SpatialIndex like entity render components, but on
			// their own layer (no Entity* behind userData — gameplay queries must not see them),
			// registered ONCE (chunks never move, so they promote straight into the static tier), and
			// WITHOUT the spawn-visibility guard (chunks stream in off-screen constantly; the guard
			// would pin each one in the main pass until it first enters the frustum).
			const Sphere bounds = resident.node.getWorldBounds();
			resident.spatialEntry = SpatialEntry(Globals::spatialIndex.registerEntry(
				glm::dvec3(bounds.pos), bounds.radius, 0ull, SpatialLayer_Terrain, false));
			m_residents.emplace(res.key, std::move(resident));
			++uploads;
		}

		const SpatialCullingConfig& culling = Globals::spatialIndex.getCullingConfig();
		const bool gate = culling.mode >= int(ESpatialCullMode::Cull);

		// --- Evict residents, but keep a column's current chunk until its replacement is ready (no hole
		// while a new LOD streams in). Evict a resident only if its column left the ring entirely, or its
		// column now wants a different LOD AND that replacement chunk is resident AND can take over ON
		// SCREEN: a freshly registered replacement isn't main-stamped until the next markVisibleSet, so
		// evicting on residency alone opened a one-frame hole on every in-view LOD change (and while the
		// culling is FROZEN the replacement never gets stamped — the old chunk just stays).
		for (auto it = m_residents.begin(); it != m_residents.end(); )
		{
			const Resident& res = it->second;
			const int want = ringLod(res.coord);
			bool evict;
			if (want < 0)
				evict = true; // column outside the ring
			else if ((uint32)want == res.lod)
				evict = false; // this is the wanted LOD
			else
			{
				const auto repIt = m_residents.find(chunkKey(res.coord, (uint32)want));
				evict = repIt != m_residents.end();
				if (evict && gate)
				{
					// Hole-free handover: the replacement is main-visible, or the old chunk isn't
					// on screen either (an off-screen swap can't show a hole).
					const bool newVis = repIt->second.spatialEntry.isValid()
						&& (Globals::spatialIndex.getPassMask(repIt->second.spatialEntry.handle()) & SpatialPassBit_Main);
					const bool oldVis = res.spatialEntry.isValid()
						&& (Globals::spatialIndex.getPassMask(res.spatialEntry.handle()) & SpatialPassBit_Main);
					evict = newVis || !oldVis;
				}
			}

			if (evict)
				it = m_residents.erase(it);
			else
				++it;
		}

		// --- Push resident chunks through the spatial culling gate. Main-visible chunks feed every pass;
		// main-culled chunks KEEP their shadow/GI passes unconditionally (terrain is the ground
		// everywhere — unlike entities, it skips the Near-ball test, because dropping the ground behind
		// the camera from the TLAS/shadow maps visibly breaks GI and long sun shadows; the GPU shadow
		// cull and the TLAS range bound already refine those passes). MainOnly debug mode drops
		// main-culled chunks entirely, like entities.
		for (auto& entry : m_residents)
		{
			Resident& res = entry.second;
			if (!res.node.isValid())
				continue;
			if (gate && res.spatialEntry.isValid())
			{
				uint32 passMask = RendererVKLayout::PASS_SHADOW | RendererVKLayout::PASS_GI;
				if (Globals::spatialIndex.getPassMask(res.spatialEntry.handle()) & SpatialPassBit_Main)
					passMask = RendererVKLayout::PASS_ALL;
				else if (culling.mode == int(ESpatialCullMode::MainOnly))
					continue;
				renderer.renderNode(res.node, passMask);
			}
			else
				renderer.renderNode(res.node);
		}
	}
}
