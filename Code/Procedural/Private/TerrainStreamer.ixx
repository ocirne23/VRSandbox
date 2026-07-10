export module Procedural:TerrainStreamer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

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
		};

		void workerLoop();
		void rebuildMaps();                             // (re)builds the active generator from the tweak-backed config
		void clearResidents();
		std::shared_ptr<const ITerrainSampler> currentMaps();
		// Bakes/refreshes the fog terrain height map around the camera (Renderer::setFogTerrainHeightMap);
		// maps == nullptr means "no terrain" and clears the map. See the .cpp for the bake scheme.
		void updateFogHeightMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& maps);

		// --- Tweak-backed configuration (source of truth; the generator/ChunkParams are built from these) ---
		bool  m_enabled = false;
		int   m_generator = 1;  // 0 = V1 Climate (continents + lakes), 1 = V2 Biome (biome field first)
		int   m_seed = 62500;
		float m_chunkSize = 128.0f;
		int   m_lod0Res = 90;
		int   m_ringRadius = 16;   // max generation range from the camera chunk, in chunks
		int   m_lodStep = 2;      // chunks of ring distance per extra LOD level
		int   m_maxLod = 4;
		float m_seaLevel = 0.0f;
		float m_skirtDepth = 5.0f;
		int   m_maxUploadsPerFrame = 8;
		float m_edgeFadeChunks = 2.0f; // width (in chunks) of the height fade-in band at the generation edge

		float  m_continentAmplitude = 150.0f;
		float  m_mountainAmplitude = 300.0f;
		float  m_detailAmplitude = 6.0f;
		float  m_continentFrequency = 0.0009f;
		float  m_mountainFrequency = 0.0015f;
		float  m_detailFrequency = 0.02f;
		float  m_climateFrequency = 0.00025f;
		int    m_continentOctaves = 5;
		int    m_mountainOctaves = 6;
		int    m_detailOctaves = 4;
		float  m_warpStrength = 40.0f;
		float  m_lapseRate = 0.0018f;
		float  m_networkCell = 1200.0f;    // lake lattice spacing (m): smaller = more (and smaller) lakes
		float  m_lakeCoverage = 0.5f;      // fraction of lattice local-minima that hold a lake
		float  m_lakeDepth = 8.0f;
		float  m_fogFrequency = 0.0003f;   // regional fog thickness (fog map channel B; V1 only)
		float  m_fogCoverage = 0.5f;

		// --- V2 (biome-first) generator ---
		float  m_v2BiomeSize = 512.0f;    // typical biome extent (m)
		float  m_v2BiomeRes = 4.0f;       // biome-field texels across one biome (texel = size/resolution)
		float  m_v2BiomeBlend = 1.0f;      // blend width in texels: gradient width of ALL derived fields
		float  m_v2BorderWarp = 350.0f;    // domain warp on the biome lookup (wiggly borders)
		float  m_v2OceanFraction = 0.32f;  // fraction of the world that is Ocean
		float  m_v2ContinentFreq = 0.00004f; // continent/ocean scale (~25km: few, huge oceans)
		float  m_v2InlandRise = 120.0f;    // altitude gain (m) from coast to deep inland
		float  m_v2OceanDeepen = 40.0f;    // extra seabed depth (m) toward mid-ocean
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
		float  m_v2MtnMaskStart = 20.0f;   // altitude above sea (m) where mountain detail fades in
		float  m_v2MtnMaskFull = 120.0f;   // altitude where it reaches full amplitude
		bool   m_v2Erosion = false;         // pass 3: water accumulation + erosion (rivers/lakes via humidity)
		float  m_v2ErosionStrength = 6.0f; // max channel carve depth (m)
		float  m_v2ErosionRegion = 2048.0f;// simulation tile size (m); rivers stay within a tile

		bool m_configDirty = false; // set by Tweak onChange; consumed at the top of update()

		// --- Shared terrain-data map (fog terrain-following + regional thickness, ocean shore fallback,
		// terrain coloring; height / water level / fog|falloff|temp|hum / altitude per texel) ---
		bool  m_terrainMapEnabled = true;
		float m_terrainMapRange = 2048.0f;     // near cascade world size (m), centered on the camera
		float m_terrainMapFarRange = 16384.0f; // far cascade world size (m; same texel count, coarser texels)
		HeightMapBaker m_terrainMapBaker;
		bool  m_terrainMapUploaded = false;    // a map is live in the renderer (cleared on disable)

		// --- Threading ---
		std::thread             m_worker;
		std::mutex              m_mutex;
		std::condition_variable m_cv;
		std::deque<Request>     m_requests;
		std::vector<Result>     m_results;      // filled by worker, drained on the main thread
		std::vector<Result>     m_readyBacklog; // main-thread only: generated chunks over the per-frame upload cap
		std::shared_ptr<const ITerrainSampler> m_maps;
		uint32                  m_generation = 0;
		bool                    m_running = false;

		// --- Main-thread residency state ---
		std::unordered_map<uint64, Resident> m_residents;
		std::unordered_set<uint64>           m_pending; // requested/queued, not yet resident
	};
}
