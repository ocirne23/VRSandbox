module Procedural;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;
import Core.Tweaks;

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
		Tweak::intVar("Terrain", "LOD0 resolution", &m_lod0Res, 4, 512, 1.0f, dirty);
		Tweak::intVar("Terrain", "Range (chunks)", &m_ringRadius, 1, 64, 1.0f); // max generation radius from the camera chunk
		Tweak::floatVar("Terrain", "LOD step (chunks)", &m_lodStep, 0.1f, 4.0f, 0.1f); // LOD0 band width; each next band doubles
		Tweak::intVar("Terrain", "Max LOD", &m_maxLod, 0, 6, 1.0f);
		Tweak::intVar("Terrain", "Uploads/frame", &m_maxUploadsPerFrame, 1, 32, 1.0f);
		Tweak::floatVar("Terrain", "Sea level (m)", &m_seaLevel, -200.0f, 200.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain", "Skirt depth (m)", &m_skirtDepth, 0.0f, 64.0f, 0.5f, dirty);
		Tweak::floatVar("Terrain", "Edge fade (chunks)", &m_edgeFadeChunks, 0.0f, 16.0f, 0.25f); // 0 = off
		// ONE shared baked terrain-data map (height, water level, fog|falloff|temp|hum, altitude): the volumetric
		// fog's terrain follow + regional thickness, the ocean's far shore fallback, and the terrain
		// coloring all read these cascades. Disabling it degrades all three.
		Tweak::boolean("Terrain", "Terrain data map", &m_terrainMapEnabled);
		Tweak::floatVar("Terrain", "Data map range (m)", &m_terrainMapRange, 256.0f, 8192.0f, 32.0f);
		Tweak::floatVar("Terrain", "Data map far range (m)", &m_terrainMapFarRange, 1024.0f, 65536.0f, 256.0f);

		Tweak::floatVar("Terrain/V2", "Biome size (m)", &m_v2BiomeSize, 8.0f, 8000.0f, 10.0f, dirty);  // climate-field feature size
		Tweak::floatVar("Terrain/V2", "Biome blending", &m_v2BiomeBlend, 0.1f, 3.0f, 0.01f, dirty);   // climate-space kernel width (low = crisp biomes)
		Tweak::floatVar("Terrain/V2", "Border warp (m)", &m_v2BorderWarp, 0.0f, 1500.0f, 5.0f, dirty);
		Tweak::floatVar("Terrain/V2", "Ocean fraction", &m_v2OceanFraction, 0.0f, 1.0f, 0.01f, dirty);
		Tweak::floatVar("Terrain/V2", "Continent freq", &m_v2ContinentFreq, 0.0f, FLT_MAX, 0.00001f, dirty); // lower = bigger oceans/continents
		Tweak::floatVar("Terrain/V2", "Inland rise (m)", &m_v2InlandRise, 0.0f, 2500.0f, 1.0f, dirty);        // coast -> deep inland altitude gain
		Tweak::floatVar("Terrain/V2", "Ocean deepen (m)", &m_v2OceanDeepen, 0.0f, 1000.0f, 1.0f, dirty);
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

		rebuildMaps();

		m_running = true;
		m_worker = std::thread([this]() { workerLoop(); });
	}

	void TerrainStreamer::rebuildMaps()
	{
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
	void TerrainStreamer::updateFogHeightMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& maps)
	{
		const bool active = m_terrainMapEnabled && maps != nullptr;
		HeightMapBaker::Baked baked;
		if (m_terrainMapBaker.update(baked, active, maps, glm::vec2(camera.position.x, camera.position.z),
			glm::vec2(glm::max(m_terrainMapRange, 256.0f), m_terrainMapFarRange),
			RendererVKLayout::FOG_TERRAIN_RES, RendererVKLayout::FOG_TERRAIN_CASCADES, 4)) // RGBA: height, water level, fog|falloff|temp|hum, altitude
		{
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
			updateFogHeightMap(renderer, camera, nullptr); // no terrain -> no terrain-following fog
			return;
		}

		const float chunkSize = m_chunkSize;
		const int camCX = (int)std::floor(camera.position.x / chunkSize);
		const int camCZ = (int)std::floor(camera.position.z / chunkSize);
		const int R = glm::max(1, m_ringRadius);
		const float lodStep = glm::max(0.01f, m_lodStep);
		const uint32 maxLod = (uint32)glm::max(0, m_maxLod);

		// Edge fade: terrain heights lerp toward sea level over the outermost m_edgeFadeChunks chunks of the
		// ring, so chunks rise out of a flat far edge instead of popping in at full height. Applied in the
		// TERRAIN vertex shader relative to the live camera position (u_terrainFade). endDist sits just past
		// the visible edge (~R+1 chunks); startDist a band inward.
		if (m_edgeFadeChunks > 0.0f)
		{
			const float edgeDist = (float)(R + 1) * chunkSize;
			const float band = glm::clamp(m_edgeFadeChunks, 0.25f, (float)R) * chunkSize;
			// The fade lands 4 m UNDER the sea surface, not on it: a plane exactly at sea level z-fights
			// the calm ocean over the whole horizon ring; sunk under it, the ocean simply covers the edge.
			renderer.setTerrainFade(edgeDist - band, edgeDist, m_seaLevel, 4.0f);
		}
		else
			renderer.setTerrainFade(0.0f, 0.0f, m_seaLevel); // disabled (endDist <= startDist)

		std::shared_ptr<const ITerrainSampler> maps;
		uint32 generation;
		{
			std::lock_guard<std::mutex> lk(m_mutex);
			maps = m_maps;
			generation = m_generation;
		}

		updateFogHeightMap(renderer, camera, maps);

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
