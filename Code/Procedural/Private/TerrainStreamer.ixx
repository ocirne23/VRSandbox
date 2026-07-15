export module Procedural:TerrainStreamer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;
import Spatial;

import :TerrainSampler;
import :TerrainGenerator;
import :TerrainChunk;
import :HeightMapBaker;

export namespace Procedural
{
	// Render-only procedural terrain. Maintains a bounded ring of chunks around the camera, each generated
	// on a worker thread and turned into an ObjectContainer + RenderNode on the main thread, and pushes them
	// to the renderer every frame. LOD is chosen per chunk by ring distance; the world-continuous height
	// field keeps chunk and LOD boundaries consistent.
	//
	// A chunk that leaves the ring (or changes LOD) destroys its RenderNode then its ObjectContainer, which
	// frees the container's GPU mesh/texture/material allocations back to the renderer's free lists
	// (~ObjectContainer -> Renderer::removeObjectContainer), so residency stays bounded across a session.
	class TerrainStreamer
	{
	public:
		TerrainStreamer() = default;
		~TerrainStreamer();
		TerrainStreamer(const TerrainStreamer&) = delete;
		TerrainStreamer& operator=(const TerrainStreamer&) = delete;

		void initialize();                                     // registers Tweaks + starts the worker thread
		void update(Renderer& renderer, const Camera& camera); // per-frame: stream, drain, render (call after beginFrame)

		// The live height/climate field, for systems that must agree with the rendered terrain (the
		// ocean's shore-depth bake). nullptr while terrain rendering is disabled — consumers treat that
		// as "no terrain" rather than sampling a field that isn't drawn. Thread-safe; the shared_ptr
		// keeps the maps valid across config rebuilds (workers holding the old maps finish against them).
		std::shared_ptr<const ITerrainSampler> activeClimateMaps() { return m_enabled ? currentMaps() : nullptr; }
		// The ocean-reach rule this streamer bakes into the terrain-data map, for anything baking the SAME
		// fields off the same sampler (the ocean's shore map, which overrides that map inside its range).
		// Handed out rather than duplicated so the two cannot drift apart — see applyWaterReach.
		// nullptr = the rule is off; bake the sampler's water level as-is.
		const WaterReach* activeWaterReach() const { return m_waterReachEnabled ? &m_waterReach : nullptr; }
		// The flow-direction rule, handed out for the same reason (the ocean's shore map must bake the
		// SAME directions this streamer bakes into the terrain-data map's 8 flow bits, or the wave travel
		// direction would turn where one map hands over to the other). nullptr = flow bits carry only what
		// the generator authors. See applyFlowField.
		const FlowField* activeFlowField() const { return m_flowFieldEnabled ? &m_flowField : nullptr; }
		// The offshore heading the baked flow eases back into — the ocean's swell heading, pushed in by the
		// app each frame (the streamer cannot know it; a stale angle just re-bakes one map). See FlowField.
		void setFlowWindAngle(float radians) { m_flowField.windAngle = radians; }
		// THE sea level datum for the world, tweak-backed here because the terrain generator builds its
		// heights around it (so it regenerates chunks) — the ocean floats on this rather than owning a
		// second copy. Valid even while terrain is disabled, so the ocean can still run on its own.
		float seaLevel() const { return m_seaLevel; }

	private:
		struct Request
		{
			uint64 key = 0;
			uint32 generation = 0;
			ChunkParams params;
			std::shared_ptr<const ITerrainSampler> maps;
		};

		struct Result
		{
			uint64 key = 0;
			uint32 generation = 0;
			glm::ivec2 coord{ 0, 0 };
			uint32 lod = 0;
			TerrainChunkMesh mesh;
		};

		struct Resident
		{
			std::unique_ptr<ObjectContainer> container; // declared first -> destroyed AFTER node
			glm::ivec2 coord{ 0, 0 };
			uint32 lod = 0;
			RenderNode node;                            // references container's meshes; destroyed first
			SpatialEntry spatialEntry;                  // culling registration (SpatialLayer_Terrain, static)
		};

		void workerLoop();
		void rebuildMaps();                             // (re)builds the active generator from the tweak-backed config
		void clearResidents();
		// Pushes the splat shaping params + the live climate boxes every frame, and registers the texture
		// set once the background DDS bake finishes (the TERRAIN shader falls back to flat colors until
		// then).
		void updateTerrainTextures(Renderer& renderer);
		void registerTerrainTextures(Renderer& renderer); // one-shot, from updateTerrainTextures
		std::shared_ptr<const ITerrainSampler> currentMaps();
		// Bakes/refreshes the fog terrain height map around the camera (Renderer::setFogTerrainHeightMap);
		// maps == nullptr means "no terrain" and clears the map. See the .cpp for the bake scheme.
		void updateFogHeightMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& maps, float farRange);

		// --- Tweak-backed configuration (source of truth; the generator/ChunkParams are built from these) ---
		bool  m_enabled = true;
		int   m_seed = 62500;
		int   m_chunkSize = 512;
		int   m_lod0Res = 512;
		int   m_ringRadius = 4;   // max generation range from the camera chunk, in chunks
		float m_lodStep = 1.0f;   // LOD0 band width in chunks (fractional ok); each next LOD band is twice as wide (geometric)
		int   m_maxLod = 4;
		float m_seaLevel = 0.0f;
		float m_skirtDepth = 5.0f;
		int   m_maxUploadsPerFrame = 16;

		// --- The Terrain Diffusion generator. ONNX-model backed: it needs 2.28 GB of
		// weights on disk and a DirectML-capable GPU, and it generates 7.68 km tiles rather than evaluating
		// a point function. See Private/Diffusion/GeneratorV3.ixx.
		float m_v3MetersPerPixel = 3.0f;  // 30 = the model's true training scale; lower compresses the world
		float m_v3HeightScale = 1.0f;
		float m_v3DetailSlopeGain = 0.75f; // slope -> detail mask (the model only resolves 30 m/px)
		// Wavelengths/amplitudes are in MODEL metres and ride metersPerPixel, so the OCTAVE counts are what
		// set how far below the model's 30 m/px the terrain actually has anything in it. See the tweak
		// registration for the resolution arithmetic — this is the dial for "detailed at full world scale".
		float m_v3DetailWavelengthA = 220.0f;
		float m_v3DetailAmplitudeA = 38.0f;
		int   m_v3DetailOctavesA = 4;
		float m_v3DetailWavelengthB = 45.0f;
		float m_v3DetailAmplitudeB = 11.0f;
		int   m_v3DetailOctavesB = 3;
		float m_v3PrecipFullHumidity = 2200.0f; // mm/yr that reads as humidity 1.0
		float m_v3HumidityOffset = 0.0f;        // slides the planet along arid <-> lush
		float m_v3TemperatureOffset = 0.0f;     // C, shifts the whole planet
		float m_v3LapseRate = -0.008f;          // C per MODEL metre: THE snow-line dial. See the tweak.
		// Microclimate wander (C): breaks climate boundaries off the contour lines the lapse rate pins them
		// to. See the tweak registration.
		float m_v3ClimateNoiseC = 5.0f;
		float m_v3ClimateNoiseWavelength = 2000.0f; // model metres
		int   m_v3ClimateNoiseOctaves = 3;
		float m_v3HumidFog = 0.25f;
		float m_v3ValleyFog = 0.25f;
		int   m_v3MaxTiles = 256;           // resident tile budget (~800 KB each)
		// Half-precision inference. Buys VRAM (~2.28 GB -> ~1.1 GB) and load time, NOT generation speed —
		// the pipeline is dispatch-bound (see the tweak registration for the measurements). Needs the
		// optional models from Tools/convert_models_fp16.py; without them it stays fp32. Flipping it
		// reloads the models and changes the terrain for a given seed, so it regenerates the world.
		bool  m_v3Fp16 = false;
		// V3 can't generate until its models are downloaded+loaded. Building chunks before then would bake a
		// flat sea-level world into the resident cache, so the streamer idles instead and rebuilds on ready.
		bool m_v3AwaitingModels = false;
		double m_v3LastStatusLog = 0.0; // rate-limits the download/load progress log

		bool m_configDirty = false; // set by Tweak onChange; consumed at the top of update()

		// --- Shared terrain-data map (fog terrain-following + regional thickness, ocean shore fallback,
		// terrain coloring; height / water level / fog|falloff|temp|hum / altitude per texel) ---
		bool  m_terrainMapEnabled = true;
		bool  m_terrainMapDebugLog = false; // log the decoded climate range of every baked cascade
		float m_terrainMapRange = 2048.0f;     // near cascade world size (m), centered on the camera
		float m_terrainMapFarRange = 8192.0f;  // far cascade world size (m; same texel count, coarser texels)
		// Ocean reach: how close real ocean must be for water here to still be the sea. Beyond it the baked
		// water level sinks below the ground, which is what stops the ocean running swash up every inland
		// hollow that happens to sit within a metre of sea level (V3 models no lakes, so it reports sea
		// level planet-wide). See applyWaterReach.
		WaterReach m_waterReach;
		bool  m_waterReachEnabled = true;
		// Flow direction: which way the water moves per texel (toward land through the surf zone, downhill
		// everywhere else) — the wave-travel field at the coast and the seed data for future rivers/water
		// simulation. See applyFlowField.
		FlowField m_flowField;
		bool  m_flowFieldEnabled = true;
		HeightMapBaker m_terrainMapBaker;
		bool  m_terrainMapUploaded = false;    // a map is live in the renderer (cleared on disable)

		// --- Terrain splat textures: source images baked to BC .dds (Assets/Local/TerrainTex) on a
		// background thread at startup (skipped when the cache is fresh), then registered once ---
		std::jthread      m_texBakeWorker;
		std::atomic<bool> m_texBakeDone{ false };
		bool              m_texSetRegistered = false;
		// Climate of each entry that survived the bake, in registered material order. Kept in REAL units
		// (x,y = temperature C range, z,w = precipitation mm/yr range) and renormalized into m_texClimateBoxes
		// every frame, because the mm-per-full-humidity divisor is a live tweak.
		std::vector<glm::vec4> m_texClimateReal;
		std::vector<glm::vec4> m_texClimateBoxes;

		// --- Terrain/Textures tweaks: splat shaping, pushed to Renderer::setTerrainTextureParams every
		// frame from updateTerrainTextures (mirrors Renderer::TerrainTexTweaks) ---
		float m_texUvScaleGround = 0.20f;  // 1/m: ~5 m texture repeat on flat ground
		float m_texUvScaleRock = 0.08f;    // 1/m: rock features read larger on cliffs
		float m_texUvScaleSnow = 0.12f;    // 1/m
		float m_texClimateBlend = 0.02f;   // Gaussian sigma OUTSIDE a climate box, in (t01, h01) units
		// Slope is 1 - N.y: 0.30 = 45 deg (about where soil stops holding), 0.55 = 63 deg.
		float m_texSlopeRockStart = 0.25f;
		float m_texSlopeRockFull = 0.60f;
		// Crag wander: breaks the rock boundary off the elevation contour it otherwise traces. In metres at
		// the model's true scale, like the crag thresholds it modulates. See the tweak registration.
		float m_texCragWanderAmp = 66.0f;
		float m_texCragWanderWavelength = 400.0f;
		float m_texCragStart = 34.0f;      // crag relief start (m above macro altitude)
		float m_texCragFull = 400.0f;
		float m_texBeachBand = 2.5f;       // beach band height (m above water level)
		// Snow COVER (composited over ground and rock alike, so peaks read white with their own rock
		// showing on the steep faces). It sheds before rock appears: 0.18 = 35 deg vs rock's 45.
		// Below freezing is NOT permanent snow (Siberia averages -10 C and is forest), so these sit well
		// below 0. Raise "temp none" toward 0 for a more fantasy, snow-capped look.
		float m_texSnowTempFull = -7.0f;  // C at/below which cover is complete
		float m_texSnowTempNone = -1.0f;   // C at/above which there is none
		float m_texSnowSlopeStart = 0.26f;
		float m_texSnowSlopeFull = 0.60f;
		float m_texSnowAridity = 0.10f;    // humidity at/below which cold ground stays bare (polar desert)

		// --- Threading ---
		std::thread             m_worker;
		std::mutex              m_mutex;
		std::condition_variable m_cv;
		// An unordered POOL of outstanding work, despite the deque: the worker rescans it on every dequeue
		// and takes whichever chunk is nearest the camera THEN, so insertion order carries no meaning and
		// neither side sorts it. Ordering it would just re-decide, one camera position stale, what the
		// worker decides correctly at pick time — and FIFO is what made distant chunks generate before the
		// ground underfoot.
		std::deque<Request>     m_requests;
		// Ring state the worker judges queued requests against at DEQUEUE time (guarded by m_mutex): both
		// which are stale (dropped lazily, in the same pass) and which is nearest. m_ringR = -1 until the
		// first publish (everything queued before that would drop, but the first publish precedes the
		// first append under the same lock).
		glm::ivec2 m_ringCam{ 0, 0 };
		int    m_ringR = -1;
		float  m_ringLodStep = 1.0f;
		uint32 m_ringMaxLod = 0;
		std::vector<Result>     m_results;      // filled by worker, drained on the main thread
		std::vector<Result>     m_readyBacklog; // main-thread only: generated chunks over the per-frame upload cap
		std::shared_ptr<const ITerrainSampler> m_maps;
		uint32                  m_generation = 0;
		bool                    m_running = false;

		// --- Main-thread residency state ---
		std::unordered_map<uint64, Resident> m_residents;
		std::unordered_set<uint64>           m_pending; // requested/queued, not yet resident
		// Last ring parameters published to the worker (main-thread copies: the publish is skipped —
		// no lock taken — while none of them changed and there is nothing new to append).
		int    m_lastRingCX = INT_MIN;
		int    m_lastRingCZ = INT_MIN;
		int    m_lastRingR = -1;
		float  m_lastRingLodStep = 0.0f;
		uint32 m_lastRingMaxLod = 0xFFFFFFFFu;
	};
}
