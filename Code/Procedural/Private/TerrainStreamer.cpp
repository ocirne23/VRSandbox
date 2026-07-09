module Procedural;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;
import Core.Tweaks;

import RendererVK;
import File;

import :TerrainStreamer;
import :Climate;
import :TerrainGenerator;
import :TerrainChunk;

namespace
{
	// Pack a chunk coordinate + LOD into a stable 64-bit key. 28 bits each for X/Z covers +-134M chunks.
	uint64 chunkKey(glm::ivec2 coord, uint32 lod)
	{
		const uint64 x = (uint64)(uint32)coord.x & 0xFFFFFFFull;
		const uint64 z = (uint64)(uint32)coord.y & 0xFFFFFFFull;
		return (x << 36) | (z << 8) | (uint64)(lod & 0xFFu);
	}

	// Same packing without the LOD byte — identifies a column of chunks regardless of level.
	uint64 coordKey(glm::ivec2 coord)
	{
		const uint64 x = (uint64)(uint32)coord.x & 0xFFFFFFFull;
		const uint64 z = (uint64)(uint32)coord.y & 0xFFFFFFFull;
		return (x << 36) | (z << 8);
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
		Tweak::intVar("Terrain", "Seed", &m_seed, 0, 1000000, 1.0f, dirty);
		Tweak::floatVar("Terrain", "Chunk size (m)", &m_chunkSize, 16.0f, 1024.0f, 1.0f, dirty);
		Tweak::intVar("Terrain", "LOD0 resolution", &m_lod0Res, 4, 256, 1.0f, dirty);
		Tweak::intVar("Terrain", "Range (chunks)", &m_ringRadius, 1, 64, 1.0f); // max generation radius from the camera chunk
		Tweak::intVar("Terrain", "LOD step (chunks)", &m_lodStep, 1, 16, 1.0f);
		Tweak::intVar("Terrain", "Max LOD", &m_maxLod, 0, 6, 1.0f);
		Tweak::intVar("Terrain", "Uploads/frame", &m_maxUploadsPerFrame, 1, 32, 1.0f);
		Tweak::floatVar("Terrain", "Sea level (m)", &m_seaLevel, -200.0f, 200.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain", "Skirt depth (m)", &m_skirtDepth, 0.0f, 64.0f, 0.5f, dirty);

		Tweak::floatVar("Terrain/Noise", "Continent amp (m)", &m_continentAmplitude, 0.0f, 400.0f, 1.0f, dirty);
		Tweak::floatVar("Terrain/Noise", "Mountain amp (m)", &m_mountainAmplitude, 0.0f, 800.0f, 1.0f, dirty);
		Tweak::floatVar("Terrain/Noise", "Detail amp (m)", &m_detailAmplitude, 0.0f, 40.0f, 0.1f, dirty);
		Tweak::floatVar("Terrain/Noise", "Continent freq", &m_continentFrequency, 0.0f, FLT_MAX, 0.0001f, dirty);
		Tweak::floatVar("Terrain/Noise", "Mountain freq", &m_mountainFrequency, 0.0f, FLT_MAX, 0.0001f, dirty);
		Tweak::floatVar("Terrain/Noise", "Detail freq", &m_detailFrequency, 0.0f, FLT_MAX, 0.001f, dirty);
		Tweak::floatVar("Terrain/Noise", "Climate freq", &m_climateFrequency, 0.0f, FLT_MAX, 0.0001f, dirty);
		Tweak::intVar("Terrain/Noise", "Continent octaves", &m_continentOctaves, 1, 8, 1.0f, dirty);
		Tweak::intVar("Terrain/Noise", "Mountain octaves", &m_mountainOctaves, 1, 8, 1.0f, dirty);
		Tweak::intVar("Terrain/Noise", "Detail octaves", &m_detailOctaves, 1, 8, 1.0f, dirty);
		Tweak::floatVar("Terrain/Noise", "Warp strength (m)", &m_warpStrength, 0.0f, 200.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain/Noise", "Lapse rate", &m_lapseRate, 0.0f, 0.02f, 0.0001f, dirty);

		rebuildMaps();

		m_running = true;
		m_worker = std::thread([this]() { workerLoop(); });
	}

	void TerrainStreamer::rebuildMaps()
	{
		TerrainConfig cfg;
		cfg.seed = (uint32)m_seed;
		cfg.seaLevel = m_seaLevel;
		cfg.continentFrequency = m_continentFrequency;
		cfg.continentOctaves = (uint32)glm::max(1, m_continentOctaves);
		cfg.continentAmplitude = m_continentAmplitude;
		cfg.mountainFrequency = m_mountainFrequency;
		cfg.mountainOctaves = (uint32)glm::max(1, m_mountainOctaves);
		cfg.mountainAmplitude = m_mountainAmplitude;
		cfg.detailFrequency = m_detailFrequency;
		cfg.detailOctaves = (uint32)glm::max(1, m_detailOctaves);
		cfg.detailAmplitude = m_detailAmplitude;
		cfg.warpStrength = m_warpStrength;
		cfg.climateFrequency = m_climateFrequency;
		cfg.lapseRate = m_lapseRate;

		auto maps = std::make_shared<const ClimateMaps>(cfg);

		std::lock_guard<std::mutex> lk(m_mutex);
		m_maps = std::move(maps);
		++m_generation;
		m_requests.clear(); // drop queued work built against the old config
	}

	std::shared_ptr<const ClimateMaps> TerrainStreamer::currentMaps()
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
			{
				std::unique_lock<std::mutex> lk(m_mutex);
				m_cv.wait(lk, [this]() { return !m_running || !m_requests.empty(); });
				if (!m_running)
					return;
				req = std::move(m_requests.front());
				m_requests.pop_front();
			}

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

	void TerrainStreamer::update(Renderer& renderer, const Camera& camera)
	{
		if (m_configDirty)
		{
			m_configDirty = false;
			rebuildMaps();
			clearResidents(); // geometry/climate changed: regenerate everything against the new config
		}

		if (!m_enabled)
		{
			if (!m_residents.empty() || !m_pending.empty())
				clearResidents();
			return;
		}

		const float chunkSize = m_chunkSize;
		const int camCX = (int)std::floor(camera.position.x / chunkSize);
		const int camCZ = (int)std::floor(camera.position.z / chunkSize);
		const int R = glm::max(1, m_ringRadius);
		const int lodStep = glm::max(1, m_lodStep);
		const uint32 maxLod = (uint32)glm::max(0, m_maxLod);

		std::shared_ptr<const ClimateMaps> maps;
		uint32 generation;
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			maps = m_maps;
			generation = m_generation;
		}

		// --- Desired ring + enqueue any chunk that is neither resident nor already in flight.
		// desired      = the exact (coord,lod) keys we want resident.
		// desiredLod   = per-column target LOD, used by eviction to keep a column's current chunk alive
		//                until its new-LOD replacement is resident (avoids a hole while it streams in).
		std::unordered_set<uint64> desired;
		std::unordered_map<uint64, uint32> desiredLod;
		desired.reserve((size_t)(2 * R + 1) * (2 * R + 1));
		desiredLod.reserve((size_t)(2 * R + 1) * (2 * R + 1));
		std::vector<Request> newRequests;

		for (int dz = -R; dz <= R; ++dz)
		{
			for (int dx = -R; dx <= R; ++dx)
			{
				const int adx = dx < 0 ? -dx : dx;
				const int adz = dz < 0 ? -dz : dz;
				const int cheb = glm::max(adx, adz);
				const uint32 lod = glm::min(maxLod, (uint32)(cheb / lodStep));
				const glm::ivec2 coord(camCX + dx, camCZ + dz);
				const uint64 key = chunkKey(coord, lod);
				desired.insert(key);
				desiredLod.emplace(coordKey(coord), lod);

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

		if (!newRequests.empty())
		{
			{
				std::lock_guard<std::mutex> lk(m_mutex);
				for (Request& r : newRequests)
					m_requests.push_back(std::move(r));
			}
			m_cv.notify_all();
		}

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
			const bool valid = (res.generation == generation) && desired.count(res.key) && !m_residents.count(res.key);
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
			m_residents.emplace(res.key, std::move(resident));
			++uploads;
		}

		// --- Evict residents, but keep a column's current chunk until its replacement is ready (no hole
		// while a new LOD streams in). Evict a resident only if its column left the ring entirely, or its
		// column now wants a different LOD AND that replacement chunk is already resident.
		for (auto it = m_residents.begin(); it != m_residents.end(); )
		{
			const Resident& res = it->second;
			const auto wantIt = desiredLod.find(coordKey(res.coord));
			bool evict;
			if (wantIt == desiredLod.end())
				evict = true; // column outside the ring
			else if (wantIt->second == res.lod)
				evict = false; // this is the wanted LOD
			else
				evict = m_residents.count(chunkKey(res.coord, wantIt->second)) != 0; // replacement ready

			if (evict)
				it = m_residents.erase(it);
			else
				++it;
		}

		// --- Push every resident chunk to the renderer (GPU indirect cull handles visibility).
		for (auto& entry : m_residents)
		{
			if (entry.second.node.isValid())
				renderer.renderNode(entry.second.node);
		}
	}
}
