export module Procedural:TerrainStreamer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

import :Climate;
import :TerrainGenerator;
import :TerrainChunk;

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

	private:
		struct Request
		{
			uint64 key = 0;
			uint32 generation = 0;
			ChunkParams params;
			std::shared_ptr<const ClimateMaps> maps;
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
		void rebuildMaps();                             // (re)builds ClimateMaps from the tweak-backed config
		void clearResidents();
		std::shared_ptr<const ClimateMaps> currentMaps();

		// --- Tweak-backed configuration (source of truth; ClimateMaps/ChunkParams are built from these) ---
		bool  m_enabled = false;
		int   m_seed = 1337;
		float m_chunkSize = 128.0f;
		int   m_lod0Res = 64;
		int   m_ringRadius = 8;   // max generation range from the camera chunk, in chunks
		int   m_lodStep = 2;      // chunks of ring distance per extra LOD level
		int   m_maxLod = 4;
		float m_seaLevel = 0.0f;
		float m_skirtDepth = 8.0f;
		int   m_maxUploadsPerFrame = 4;

		float  m_continentAmplitude = 60.0f;
		float  m_mountainAmplitude = 180.0f;
		float  m_detailAmplitude = 6.0f;
		float  m_continentFrequency = 0.0009f;
		float  m_mountainFrequency = 0.0025f;
		float  m_detailFrequency = 0.02f;
		float  m_climateFrequency = 0.00025f;
		int    m_continentOctaves = 5;
		int    m_mountainOctaves = 6;
		int    m_detailOctaves = 4;
		float  m_warpStrength = 40.0f;
		float  m_lapseRate = 0.0018f;

		bool m_configDirty = false; // set by Tweak onChange; consumed at the top of update()

		// --- Threading ---
		std::thread             m_worker;
		std::mutex              m_mutex;
		std::condition_variable m_cv;
		std::deque<Request>     m_requests;
		std::vector<Result>     m_results;      // filled by worker, drained on the main thread
		std::vector<Result>     m_readyBacklog; // main-thread only: generated chunks over the per-frame upload cap
		std::shared_ptr<const ClimateMaps> m_maps;
		uint32                  m_generation = 0;
		bool                    m_running = false;

		// --- Main-thread residency state ---
		std::unordered_map<uint64, Resident> m_residents;
		std::unordered_set<uint64>           m_pending; // requested/queued, not yet resident
	};
}
