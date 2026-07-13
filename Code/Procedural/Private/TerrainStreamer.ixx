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
		// Registers the biome texture sets with the renderer once the background DDS bake finishes (the
		// TERRAIN shader falls back to flat colors until then), and keeps the climate kernel width live.
		void updateTerrainTextures(Renderer& renderer);
		std::shared_ptr<const ITerrainSampler> currentMaps();
		// Bakes/refreshes the fog terrain height map around the camera (Renderer::setFogTerrainHeightMap);
		// maps == nullptr means "no terrain" and clears the map. See the .cpp for the bake scheme.
		void updateFogHeightMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& maps, float farRange);

		// --- Tweak-backed configuration (source of truth; the generator/ChunkParams are built from these) ---
		bool  m_enabled = true;
		int   m_seed = 62500;
		int   m_chunkSize = 512;
		int   m_lod0Res = 512;
		int   m_ringRadius = 32;   // max generation range from the camera chunk, in chunks
		float m_lodStep = 1.0f;   // LOD0 band width in chunks (fractional ok); each next LOD band is twice as wide (geometric)
		int   m_maxLod = 4;
		float m_seaLevel = 0.0f;
		float m_skirtDepth = 5.0f;
		int   m_maxUploadsPerFrame = 16;

		// --- V2 (climate-first) generator ---
		float  m_detailFrequency = 0.02f;  // height detail fBm (cycles/m)
		int    m_detailOctaves = 4;
		float  m_v2BiomeSize = 512.0f;    // typical biome extent (m): climate-field feature size
		float  m_v2BiomeBlend = 1.0f;      // climate-space kernel width (low = crisp biome identity)
		float  m_v2BorderWarp = 300.0f;    // domain warp on the biome lookup (wiggly borders)
		float  m_v2OceanFraction = 0.42f;  // fraction of the world that is Ocean
		float  m_v2ContinentFreq = 0.00005f; // continent/ocean scale (~25km: few, huge oceans)
		float  m_v2InlandRise = 1024.0;    // altitude gain (m) from coast to deep inland
		float  m_v2OceanDeepen = 400.0f;    // extra seabed depth (m) toward mid-ocean
		float  m_v2TempLapse = 0.0012f;    // temperature drop per meter of altitude (snowy peaks)
		float  m_v2ClimateBandAmp = 0.25f; // continent-scale hot/cold band strength
		float  m_v2HeightScale = 1.0f;     // global multiplier on the biome height table
		float  m_v2AltitudeTexel = 64.0f;  // altitude map texel size (m): between biome map and fine fields
		float  m_v2AltitudeAmp = 60.0f;    // large-scale rolling-relief swing (m)
		float  m_v2AltitudeFreq = 0.0002f; // relief noise frequency (cycles/m)
		float  m_v2PeakAmp = 250.0f;       // fantasy peak height (m); per-biome relief scale damps flats
		float  m_v2PeakThreshold = 0.5f;   // ridged-field value where peaks start (higher = rarer ranges)
		float  m_v2PeakSharpness = 1.8f;   // peak curve power (higher = steeper)
		float  m_v2PeakFreq = 0.00025f;    // range placement scale (~4km: more, smaller ranges)
		float  m_v2MtnDetailAmp = 90.0f;   // altitude-masked ridge detail on mountains (m)
		float  m_v2MtnDetailFreq = 0.004f; // ridge detail scale (~250m features)
		float  m_v2MtnMaskStart = 40.0f;   // macro RELIEF above the local baseline (m) where mountain detail fades in
		float  m_v2MtnMaskFull = 150.0f;   // macro relief where it reaches full amplitude
		float  m_v2GrassFog = 0.5f;        // grassland low-fog patch thickness (0 = off)
		float  m_v2ValleyFog = 0.8f;       // mountain-valley fog thickness (0 = off)

		bool m_configDirty = false; // set by Tweak onChange; consumed at the top of update()

		// --- Shared terrain-data map (fog terrain-following + regional thickness, ocean shore fallback,
		// terrain coloring; height / water level / fog|falloff|temp|hum / altitude per texel) ---
		bool  m_terrainMapEnabled = true;
		float m_terrainMapRange = 2048.0f;     // near cascade world size (m), centered on the camera
		float m_terrainMapFarRange = 8192.0f;  // far cascade world size (m; same texel count, coarser texels)
		HeightMapBaker m_terrainMapBaker;
		bool  m_terrainMapUploaded = false;    // a map is live in the renderer (cleared on disable)

		// --- Terrain splat textures: source images baked to BC .dds (Assets/Local/TerrainTex) on a
		// background thread at startup (skipped when the cache is fresh), then registered once ---
		std::jthread      m_texBakeWorker;
		std::atomic<bool> m_texBakeDone{ false };
		bool              m_texSetRegistered = false;

		// --- Terrain/Textures tweaks: splat shaping, pushed to Renderer::setTerrainTextureParams every
		// frame from updateTerrainTextures (mirrors Renderer::TerrainTexTweaks) ---
		float m_texUvScaleGround = 0.20f;  // 1/m: ~5 m texture repeat on flat ground
		float m_texUvScaleRock = 0.08f;    // 1/m: rock features read larger on cliffs
		float m_texSlopeRockStart = 0.55f;
		float m_texSlopeRockFull = 0.75f;
		float m_texCragStart = 12.0f;      // crag relief start (m above macro altitude)
		float m_texCragFull = 50.0f;
		float m_texBeachBand = 2.5f;       // beach band height (m above water level)
		float m_texBlendScale = 1.5f;      // multiplies the climate kernel sigma for TEXTURE blends only

		// --- Threading ---
		std::thread             m_worker;
		std::mutex              m_mutex;
		std::condition_variable m_cv;
		std::deque<Request>     m_requests;
		// Ring state the worker validates queued requests against at DEQUEUE time (guarded by m_mutex):
		// stale requests drop lazily when popped — nobody rewrites the queue. m_ringR = -1 until the
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
