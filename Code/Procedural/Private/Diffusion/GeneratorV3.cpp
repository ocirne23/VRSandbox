module Procedural;

import Core;
import Core.Log;
import :GeneratorV3;
import :Noise;
import :Diffusion.ModelAssets;
import :Diffusion.Onnx;
import :Diffusion.Pipeline;
import :Diffusion.Rng;
import :Diffusion.Tensor;
import :TerrainSampler;

namespace Procedural
{
	namespace
	{
		using namespace Procedural::Diffusion;

		// Full-detail tile: native model pixels (30 m each by default), so one tile is 7.68 km.
		constexpr int32 TILE = 256;
		constexpr int32 HALO = 1; // one pixel each side, so bilinear/gradient never crosses a tile edge
		constexpr int32 TILE_W = TILE + 2 * HALO;

		// Coarse tile: the COARSE STAGE's own lattice, where one pixel is 256 native pixels. A 64-pixel
		// coarse tile therefore spans 16,384 native pixels — ~491 km at 30 m/px. That is the whole point of
		// the coarse path: a wide-area query resolves to one or two of these instead of the thousands of
		// full-detail tiles the same area would otherwise demand.
		constexpr int32 CTILE = 64;
		constexpr int32 CHALO = 1;
		constexpr int32 CTILE_W = CTILE + 2 * CHALO;

		// One cached rectangle of the field. Both detail levels share this shape; `width` says which.
		struct FieldTile
		{
			std::vector<float> elev;   // metres in the MODEL's frame, SIGNED (seabed stays negative)
			std::vector<float> macro;  // the low-frequency band of elev: the smooth surface it rides on
			std::vector<float> temp;   // degrees C
			std::vector<float> precip; // mm/yr
			int32 width = 0;           // TILE_W or CTILE_W
		};
		using FieldTilePtr = std::shared_ptr<const FieldTile>;

		uint64 tileKey(int32 ti, int32 tj)
		{
			return ((uint64)(uint32)ti << 32) | (uint64)(uint32)tj;
		}

		// -------------------------------------------------------------------------------------------------
		// The process-lifetime runtime: models are 2.28 GB, so they load ONCE and are reseeded, never
		// rebuilt. Every TerrainGenV3 shares this; constructing a generator is consequently cheap, which
		// matters because TerrainStreamer::rebuildMaps() makes a fresh one on every tweak change.
		// -------------------------------------------------------------------------------------------------
		class DiffusionRuntime
		{
		public:
			static DiffusionRuntime& get()
			{
				static DiffusionRuntime r;
				return r;
			}

			void beginLoad()
			{
				std::lock_guard<std::mutex> lk(m_loadMutex);
				if (m_loadStarted)
					return;
				m_loadStarted = true;
				// The assets are on disk (shipped with the repo), but getting 2.28 GB of ONNX into DirectML
				// still takes seconds, so it stays off the main thread.
				m_loader = std::thread([this]() { loadWorker(); });
			}

			bool isReady() const { return m_ready.load(std::memory_order_acquire); }
			bool hasFailed() const { return m_failed.load(std::memory_order_acquire); }
			// The model's own resolution, in metres per pixel (30 in the shipped config).
			float nativeResolution() const { return m_nativeResolution.load(std::memory_order_relaxed); }
			// Native pixels per coarse pixel (256 in the shipped config).
			int32 nativePerCoarsePixel() const { return m_nativePerCoarse.load(std::memory_order_relaxed); }

			std::string statusText() const
			{
				if (m_ready.load(std::memory_order_acquire))
					return "Ready";
				std::lock_guard<std::mutex> lk(m_statusMutex);
				if (m_failed.load(std::memory_order_acquire))
					return m_assets.error().empty() ? "Failed - see log" : m_assets.error();
				return m_status;
			}

			// Blocking on a miss. Serialised through m_pipelineMutex: the tile store is not thread-safe.
			FieldTilePtr fetchTile(int32 ti, int32 tj);
			// The coarse-stage counterpart. Also blocking, but a tiny fraction of the cost per unit area.
			FieldTilePtr fetchCoarseTile(int32 ti, int32 tj);

			void setSeed(uint64 seed)
			{
				if (!isReady())
					return;
				std::lock_guard<std::mutex> lk(m_pipelineMutex);
				if (seed == m_pipeline->seed())
					return;
				m_pipeline->setSeed(seed);
				{
					std::lock_guard<std::mutex> ck(m_cacheMutex);
					m_cache.clear();
					m_coarseCache.clear();
				}
				Log::info(std::format("[Diffusion] reseeded to {}", seed));
			}

			void setMaxResidentTiles(int32 n)
			{
				std::lock_guard<std::mutex> ck(m_cacheMutex);
				m_maxTiles = std::max(4, n);
				evictLocked();
			}

		private:
			DiffusionRuntime() = default;
			~DiffusionRuntime()
			{
				if (m_loader.joinable())
					m_loader.join();
			}

			void setStatus(std::string_view s)
			{
				std::lock_guard<std::mutex> lk(m_statusMutex);
				m_status.assign(s);
			}

			void loadWorker()
			{
				setStatus("Locating models...");
				if (!m_assets.load(EAssetSet::Full))
				{
					m_failed.store(true, std::memory_order_release);
					return;
				}
				m_nativeResolution.store(m_assets.config().nativeResolution, std::memory_order_relaxed);
				m_nativePerCoarse.store(32 * m_assets.config().latentCompression, std::memory_order_relaxed);

				setStatus("Loading models onto the GPU...");
				auto p = std::make_unique<WorldPipeline>();
				if (!p->initialize(m_seedAtLoad, m_assets.config(), m_assets.data(),
				                   EInferenceDevice::Gpu, /*coarseOnly*/ false))
				{
					m_failed.store(true, std::memory_order_release);
					return;
				}
				{
					std::lock_guard<std::mutex> lk(m_pipelineMutex);
					m_pipeline = std::move(p);
				}
				m_ready.store(true, std::memory_order_release);
				Log::info("[Diffusion] V3 terrain ready");
			}

			// Access-ordered LRU. Splicing on touch is O(1) and never invalidates the iterators the map
			// holds. (A deque + std::find was O(residentTiles) per LOOKUP, and lookups happen per sample.)
			struct Lru
			{
				struct Entry
				{
					uint64 key;
					FieldTilePtr value;
				};
				std::list<Entry> order; // front = least recently used
				std::unordered_map<uint64, std::list<Entry>::iterator> map;

				FieldTilePtr get(uint64 key)
				{
					auto it = map.find(key);
					if (it == map.end())
						return nullptr;
					order.splice(order.end(), order, it->second);
					return it->second->value;
				}
				void put(uint64 key, FieldTilePtr v)
				{
					auto it = map.find(key);
					if (it != map.end())
					{
						it->second->value = std::move(v);
						order.splice(order.end(), order, it->second);
						return;
					}
					order.push_back(Entry{ key, std::move(v) });
					map[key] = std::prev(order.end());
				}
				void trim(size_t maxSize)
				{
					while (order.size() > maxSize)
					{
						map.erase(order.front().key);
						order.pop_front();
					}
				}
				void clear()
				{
					order.clear();
					map.clear();
				}
			};

			void evictLocked()
			{
				m_cache.trim((size_t)std::max(4, m_maxTiles));
				// Coarse tiles are ~52 KB and each covers hundreds of km, so a small fixed budget is plenty —
				// and they must never be evicted by full-detail pressure, since they ARE the cheap fallback.
				m_coarseCache.trim(64);
			}

			ModelAssets m_assets;
			std::thread m_loader;
			std::mutex m_loadMutex;
			bool m_loadStarted = false;
			uint64 m_seedAtLoad = 1337;
			std::atomic<bool> m_ready{ false };
			std::atomic<bool> m_failed{ false };
			std::atomic<float> m_nativeResolution{ 30.0f };
			std::atomic<int32> m_nativePerCoarse{ 256 };
			mutable std::mutex m_statusMutex;
			std::string m_status;

			// The pipeline is single-threaded by construction; this lock IS the inference thread.
			std::mutex m_pipelineMutex;
			std::unique_ptr<WorldPipeline> m_pipeline;

			std::mutex m_cacheMutex;
			Lru m_cache;
			Lru m_coarseCache;
			int32 m_maxTiles = 64;
			// Dedups concurrent requests for the same tile so N callers cause 1 generation, not N.
			std::unordered_map<uint64, std::shared_future<FieldTilePtr>> m_pending;
			std::mutex m_pendingMutex;
		};

		FieldTilePtr DiffusionRuntime::fetchTile(int32 ti, int32 tj)
		{
			const uint64 key = tileKey(ti, tj);
			{
				std::lock_guard<std::mutex> ck(m_cacheMutex);
				if (FieldTilePtr hit = m_cache.get(key))
					return hit;
			}

			// Not resident. Claim the generation, or join whoever already claimed it, so N concurrent
			// callers cause one generation rather than N.
			std::shared_future<FieldTilePtr> fut;
			std::shared_ptr<std::packaged_task<FieldTilePtr()>> ownerTask;
			bool owner = false;
			{
				std::lock_guard<std::mutex> pk(m_pendingMutex);
				auto it = m_pending.find(key);
				if (it != m_pending.end())
				{
					fut = it->second;
				}
				else
				{
					auto task = std::make_shared<std::packaged_task<FieldTilePtr()>>([this, ti, tj]() -> FieldTilePtr
					{
						// One inference at a time: the tile store is not thread-safe.
						std::lock_guard<std::mutex> lk(m_pipelineMutex);
						if (!m_pipeline)
							return nullptr;

						const int32 i1 = ti * TILE - HALO, i2 = (ti + 1) * TILE + HALO;
						const int32 j1 = tj * TILE - HALO, j2 = (tj + 1) * TILE + HALO;

						std::vector<float> elev, climate, macro;
						if (!m_pipeline->get(i1, j1, i2, j2, /*withClimate*/ true, elev, climate, &macro))
							return nullptr;

						const size_t plane = (size_t)TILE_W * TILE_W;
						auto t = std::make_shared<FieldTile>();
						t->width = TILE_W;
						t->elev = std::move(elev);
						t->macro = std::move(macro);
						t->temp.assign(climate.begin(), climate.begin() + (ptrdiff_t)plane);
						t->precip.assign(climate.begin() + (ptrdiff_t)(2 * plane),
						                 climate.begin() + (ptrdiff_t)(3 * plane));
						return t;
					});
					fut = std::shared_future<FieldTilePtr>(task->get_future());
					m_pending[key] = fut;
					ownerTask = task;
					owner = true;
				}
			}

			// The claimant generates on its own thread; everyone else waits on the future. Callers are
			// already worker threads that expect to block (the terrain streamer worker, the scatter worker,
			// the height-map baker's std::async), and m_pipelineMutex is what actually serialises inference —
			// so a dedicated inference thread would only add a hop and a thread per tile.
			if (ownerTask)
				(*ownerTask)();

			FieldTilePtr tile = fut.get();

			if (owner)
			{
				{
					std::lock_guard<std::mutex> pk(m_pendingMutex);
					m_pending.erase(key);
				}
				if (tile)
				{
					std::lock_guard<std::mutex> ck(m_cacheMutex);
					m_cache.put(key, tile);
					evictLocked();
				}
			}
			return tile;
		}

		FieldTilePtr DiffusionRuntime::fetchCoarseTile(int32 ti, int32 tj)
		{
			const uint64 key = tileKey(ti, tj);
			{
				std::lock_guard<std::mutex> ck(m_cacheMutex);
				if (FieldTilePtr hit = m_coarseCache.get(key))
					return hit;
			}

			// No pending-dedup here, unlike fetchTile: a coarse tile is a handful of 64x64 model calls, and
			// cheapness is the entire point of this path. Two threads racing the same tile is a rare, small
			// waste; the pipeline lock below already serialises them.
			FieldTilePtr tile;
			{
				std::lock_guard<std::mutex> lk(m_pipelineMutex);
				if (!m_pipeline)
					return nullptr;
				{
					// Someone may have filled it while we waited on the pipeline lock.
					std::lock_guard<std::mutex> ck(m_cacheMutex);
					if (FieldTilePtr hit = m_coarseCache.get(key))
						return hit;
				}

				const int32 i0 = ti * CTILE - CHALO, i1 = (ti + 1) * CTILE + CHALO;
				const int32 j0 = tj * CTILE - CHALO, j1 = (tj + 1) * CTILE + CHALO;
				const FloatTensor slice = m_pipeline->getCoarseSlice(i0, j0, i1, j1);

				const size_t plane = (size_t)CTILE_W * CTILE_W;
				assert(slice.data.size() == 7 * plane);
				auto t = std::make_shared<FieldTile>();
				t->width = CTILE_W;
				t->elev.resize(plane);
				t->temp.resize(plane);
				t->precip.resize(plane);
				for (size_t i = 0; i < plane; i++)
				{
					const float w = slice.data[6 * plane + i];
					const float inv = (w > 1e-6f) ? 1.0f / w : 0.0f;
					// Undo the model's signed-sqrt elevation compression, keeping the SIGN: the far cascade
					// feeds the fog and the ocean, which need the seabed, unlike the pipeline's own climate
					// path which clamps ocean to zero.
					const float e = slice.data[0 * plane + i] * inv;
					t->elev[i] = (e > 0.0f ? 1.0f : (e < 0.0f ? -1.0f : 0.0f)) * e * e;
					t->temp[i] = slice.data[2 * plane + i] * inv;
					t->precip[i] = slice.data[4 * plane + i] * inv;
				}
				// At 7.68 km per pixel the coarse level IS the macro surface — there is no crag relief to
				// resolve here, so macro == elev and (elev - macro) is zero. Correct rather than convenient:
				// the far cascade genuinely cannot tell a crag from flat ground.
				t->macro = t->elev;
				tile = t;
			}

			std::lock_guard<std::mutex> ck(m_cacheMutex);
			m_coarseCache.put(key, tile);
			evictLocked();
			return tile;
		}
	}

	// =====================================================================================================
	// TerrainGenV3
	// =====================================================================================================

	struct TerrainGenV3::Impl
	{
		TerrainConfigV3 cfg;
		NoiseField detailA;
		NoiseField detailB;
		float invMpp = 1.0f / 30.0f;

		// One evaluation of the diffusion field at a world position.
		struct Sample
		{
			float elev = 0.0f;     // metres in the MODEL's frame (before worldScale/heightScale)
			float macro = 0.0f;    // the low-frequency band of elev, same frame
			float temp = 15.0f;    // C
			float precip = 800.0f; // mm/yr
			float slope = 0.0f;    // m/m in WORLD space (already through worldScale * heightScale)
			bool valid = false;
		};

		// A block of tiles covering a query region, fetched UP FRONT — one shared-cache lock per tile — and
		// then sampled with no locking at all. This is what sampleGrid buys over per-point fetching: a
		// 512x512 bake resolves to a couple of dozen tiles, not a quarter-million lock/unlock pairs.
		struct TileBlock
		{
			std::vector<FieldTilePtr> tiles; // row-major over [ti0, ti0+th) x [tj0, tj0+tw)
			int32 ti0 = 0, tj0 = 0, tw = 0, th = 0;
			int32 span = 0;                  // lattice pixels per tile (TILE or CTILE)
			int32 halo = 0;
			// lattice coordinate = world * scale + offset.
			// The offset is NOT decoration: the coarse lattice is PIXEL-CENTRED with respect to the native
			// one. WorldPipeline::computeClimate maps native -> coarse as (native + 0.5)/S - 0.5, i.e. coarse
			// pixel c is centred at native (c + 0.5)*S. Sampling it as native/S instead shifts the entire
			// coarse field by HALF A COARSE PIXEL — 128 native pixels, which is 3.8 km at 30 m/px — so the
			// far cascade's climate lands somewhere else than the near cascade's and the textures disagree
			// for the same position.
			double latticeScale = 0.0;
			double latticeOffset = 0.0;
			bool coarse = false;

			double latticeX(double worldX) const { return worldX * latticeScale + latticeOffset; }
			double latticeZ(double worldZ) const { return worldZ * latticeScale + latticeOffset; }
			double worldPerLattice() const { return latticeScale > 0.0 ? 1.0 / latticeScale : 0.0; }

			const FieldTile* tileAt(int32 ti, int32 tj) const
			{
				const int32 ri = ti - ti0, rj = tj - tj0;
				if (ri < 0 || ri >= th || rj < 0 || rj >= tw)
					return nullptr;
				return tiles[(size_t)ri * tw + rj].get();
			}
		};

		// metersPerPixel is a true world SCALE, not just a horizontal one: it shrinks elevation by the same
		// factor, so a compressed world keeps the proportions the model drew (same slopes, same shape, just
		// smaller). heightScale is then a pure vertical exaggeration on top.
		float worldScale() const
		{
			const float nr = DiffusionRuntime::get().nativeResolution();
			return cfg.metersPerPixel / (nr > 0.0f ? nr : 30.0f);
		}
		// Model metres -> world metres.
		float vertScale() const { return worldScale() * cfg.heightScale; }

		// Resolve every tile a world-space rect touches. Returns false if the models aren't up yet.
		bool resolveBlock(double x0, double z0, double x1, double z1, bool coarse, TileBlock& out) const
		{
			DiffusionRuntime& rt = DiffusionRuntime::get();
			if (!rt.isReady())
				return false;

			out.coarse = coarse;
			out.span = coarse ? CTILE : TILE;
			out.halo = coarse ? CHALO : HALO;

			if (!coarse)
			{
				out.latticeScale = (double)invMpp; // world -> native pixels, 1:1 with the tile lattice
				out.latticeOffset = 0.0;
			}
			else
			{
				const int32 npc = rt.nativePerCoarsePixel();
				if (npc <= 0)
					return false;
				// coarse = (native + 0.5)/npc - 0.5, matching WorldPipeline::computeClimate exactly.
				out.latticeScale = (double)invMpp / (double)npc;
				out.latticeOffset = 0.5 / (double)npc - 0.5;
			}
			if (out.latticeScale <= 0.0)
				return false;

			// +-1 lattice pixel of slack so the bilinear/gradient taps at the rect's edge stay in range.
			const int32 j0 = (int32)std::floor(out.latticeX(x0)) - 1;
			const int32 j1 = (int32)std::floor(out.latticeX(x1)) + 1;
			const int32 i0 = (int32)std::floor(out.latticeZ(z0)) - 1;
			const int32 i1 = (int32)std::floor(out.latticeZ(z1)) + 1;

			out.ti0 = floorDiv(i0, out.span);
			out.tj0 = floorDiv(j0, out.span);
			out.th = floorDiv(i1, out.span) - out.ti0 + 1;
			out.tw = floorDiv(j1, out.span) - out.tj0 + 1;
			if (out.th <= 0 || out.tw <= 0)
				return false;

			out.tiles.assign((size_t)out.th * out.tw, nullptr);
			for (int32 ti = 0; ti < out.th; ti++)
				for (int32 tj = 0; tj < out.tw; tj++)
					out.tiles[(size_t)ti * out.tw + tj] = coarse
						? rt.fetchCoarseTile(out.ti0 + ti, out.tj0 + tj)
						: rt.fetchTile(out.ti0 + ti, out.tj0 + tj);
			return true;
		}

		// Sample a point from an already-resolved block. No locks, no fetches.
		Sample sampleFromBlock(const TileBlock& b, double worldX, double worldZ) const
		{
			Sample s;
			// j <-> x, i <-> z (the pipeline's i is the row axis).
			const double nj = b.latticeX(worldX);
			const double ni = b.latticeZ(worldZ);
			const int32 j0 = (int32)std::floor(nj);
			const int32 i0 = (int32)std::floor(ni);
			const float fj = (float)(nj - (double)j0);
			const float fi = (float)(ni - (double)i0);

			const int32 ti = floorDiv(i0, b.span), tj = floorDiv(j0, b.span);
			const FieldTile* tile = b.tileAt(ti, tj);
			if (!tile)
				return s;

			const int32 w = tile->width;
			const int32 li = i0 - (ti * b.span - b.halo);
			const int32 lj = j0 - (tj * b.span - b.halo);
			assert(li >= 0 && li + 1 < w && lj >= 0 && lj + 1 < w);

			auto bilerp = [&](const std::vector<float>& v) -> float
			{
				const float a = v[(size_t)li * w + lj];
				const float b2 = v[(size_t)li * w + lj + 1];
				const float c = v[(size_t)(li + 1) * w + lj];
				const float d = v[(size_t)(li + 1) * w + lj + 1];
				return (1 - fi) * ((1 - fj) * a + fj * b2) + fi * ((1 - fj) * c + fj * d);
			};

			s.elev = bilerp(tile->elev);
			s.macro = bilerp(tile->macro);
			s.temp = bilerp(tile->temp);
			s.precip = bilerp(tile->precip);

			// Central differences over the lattice -> WORLD slope. The halo is what lets this work at a tile
			// edge without a seam. The vertical scaling matters: without it, compressing the world would
			// silently steepen every slope, which then skews the detail mask and the fog. With it, worldScale
			// cancels and the slope depends only on heightScale — i.e. proportions hold.
			const float eL = tile->elev[(size_t)li * w + (lj > 0 ? lj - 1 : lj)];
			const float eR = tile->elev[(size_t)li * w + lj + 1];
			const float eU = tile->elev[(size_t)(li > 0 ? li - 1 : li) * w + lj];
			const float eD = tile->elev[(size_t)(li + 1) * w + lj];
			const float step = 2.0f * (float)b.worldPerLattice(); // world metres between the two taps
			const float vs = vertScale();
			const float dx = (eR - eL) * vs / step;
			const float dz = (eD - eU) * vs / step;
			s.slope = std::sqrt(dx * dx + dz * dz);
			s.valid = true;
			return s;
		}

		// The point path: resolve a one-tile block and sample it.
		Sample sampleField(double worldX, double worldZ, bool coarse = false) const
		{
			TileBlock b;
			if (!resolveBlock(worldX, worldZ, worldX, worldZ, coarse, b))
				return Sample{};
			return sampleFromBlock(b, worldX, worldZ);
		}

		// The slope mask: sf^2 * sqrt(sf), so detail ramps in sharply and plains stay perfectly smooth.
		float slopeMask(float slope) const
		{
			const float sf = std::min(1.0f, slope * cfg.detailSlopeGain);
			return sf * sf * std::sqrt(sf);
		}

		// Sub-model-pixel relief, in WORLD metres. Both the wavelengths and the amplitudes ride worldScale,
		// so a compressed world gets proportionally finer, shallower crags rather than the same absolute
		// ones. Not affected by heightScale: the detail amplitudes are already direct tweaks in metres.
		float detail(double worldX, double worldZ, float slope) const
		{
			const float mask = slopeMask(slope);
			if (mask <= 0.0f)
				return 0.0f;
			const float scale = worldScale();
			const float fA = 1.0f / (cfg.detailWavelengthA * scale);
			const float fB = 1.0f / (cfg.detailWavelengthB * scale);
			const float a = detailA.fbm((float)(worldX * fA), (float)(worldZ * fA), cfg.detailOctavesA);
			const float b = detailB.fbm((float)(worldX * fB), (float)(worldZ * fB), cfg.detailOctavesB);
			return (a * cfg.detailAmplitudeA + b * cfg.detailAmplitudeB) * mask * scale;
		}

		// Climate lives in the MODEL's elevation frame and stays there. metersPerPixel and heightScale are
		// presentation choices — we draw the same planet at a different size, not relocate it — so the
		// climate rides along unscaled and a shrunk world keeps the snow caps and biome bands of the
		// full-size one. Only the procedural detail adds relief the model never saw, so it alone earns a
		// lapse correction (converted back into model metres to stay in that frame).
		float temperatureFrom(const Sample& s, float detailWorldM) const
		{
			const float detailModelM = detailWorldM / std::max(0.01f, vertScale());
			float t = s.temp - 0.0065f * std::max(0.0f, detailModelM);
			t -= cfg.extraLapseRate * std::max(0.0f, s.elev); // snow-line control, model frame
			return t + cfg.temperatureOffset;
		}

		float humidityOf(const Sample& s) const
		{
			const float h = s.precip / std::max(1.0f, cfg.precipForFullHumidity) + cfg.humidityOffset;
			return std::clamp(h, 0.0f, 1.0f);
		}

		float fogThicknessOf(const Sample& s) const
		{
			// Two contributions, both keyed off the model's real climate: wet regions carry broad haze, and
			// flat land collects valley fog.
			const float humidity = humidityOf(s);
			const float humid = cfg.humidFogAmount * humidity * humidity;
			const float flat = 1.0f - std::min(1.0f, s.slope * 2.0f);
			const float land = s.elev > 0.0f ? 1.0f : 0.0f;
			const float valley = cfg.valleyFogAmount * flat * land * humidity;
			return std::clamp(std::max(humid, valley), 0.0f, 1.0f);
		}

		float fogFalloffOf(float temperature) const
		{
			// Cold air hugs the ground; warm humid air lets fog tower.
			const float t01 = std::clamp((temperature + 10.0f) / 40.0f, 0.0f, 1.0f);
			return std::clamp(1.8f - 1.2f * t01, 0.0f, FOG_FALLOFF_MUL_MAX);
		}

		// Everything from one field evaluation. `withDetail` is false on the coarse path: sub-model-pixel
		// noise is meaningless at a far cascade's texel density and would only cost noise lookups.
		void fill(double worldX, double worldZ, const Sample& s, bool withDetail, TerrainPoint& out) const
		{
			if (!s.valid)
			{
				out = TerrainPoint{};
				out.height = cfg.seaLevel;
				out.waterLevel = cfg.seaLevel;
				return;
			}
			const float d = withDetail ? detail(worldX, worldZ, s.slope) : 0.0f;
			const float vs = vertScale();
			// altitude is the MACRO band, NOT the full surface. The terrain shader's rock layer keys on the
			// crag relief (height - altitude) to tell a mountain from flat ground at altitude, so reporting
			// the full elevation here made that difference the detail layer alone (a few metres) and no
			// slope ever grew rock — mountain tops came out textured as whatever biome the lowland was.
			out.altitude = s.macro * vs;
			out.height = cfg.seaLevel + s.elev * vs + d;
			out.waterLevel = cfg.seaLevel;
			out.temperature = temperatureFrom(s, d);
			out.humidity = humidityOf(s);
			out.fogThickness = fogThicknessOf(s);
			out.fogFalloffMul = fogFalloffOf(out.temperature);
			out.flowAngle01 = -1.0f;
		}
	};

	TerrainGenV3::TerrainGenV3(const TerrainConfigV3& cfg) : m_impl(std::make_unique<Impl>())
	{
		m_impl->cfg = cfg;
		m_impl->cfg.metersPerPixel = std::max(0.5f, cfg.metersPerPixel);
		m_impl->invMpp = 1.0f / m_impl->cfg.metersPerPixel;
		m_impl->detailA.setSeed(cfg.seed * 0x9E3779B9u + 0x51ED2701u);
		m_impl->detailB.setSeed(cfg.seed * 0x85EBCA6Bu + 0xC2B2AE35u);

		DiffusionRuntime::get().beginLoad();
		DiffusionRuntime::get().setSeed((uint64)cfg.seed);
		DiffusionRuntime::get().setMaxResidentTiles(cfg.maxResidentTiles);
	}

	TerrainGenV3::~TerrainGenV3() = default;

	void TerrainGenV3::beginLoad() { DiffusionRuntime::get().beginLoad(); }
	bool TerrainGenV3::isReady() { return DiffusionRuntime::get().isReady(); }
	bool TerrainGenV3::hasFailed() { return DiffusionRuntime::get().hasFailed(); }
	std::string TerrainGenV3::statusText() { return DiffusionRuntime::get().statusText(); }

	const TerrainConfigV3& TerrainGenV3::config() const { return m_impl->cfg; }
	float TerrainGenV3::seaLevel() const { return m_impl->cfg.seaLevel; }

	float TerrainGenV3::sampleAltitude(double worldX, double worldZ) const
	{
		// The MACRO band (see Impl::fill): the smooth surface the high-frequency residual rides on, which is
		// what makes height - altitude a meaningful "how craggy is this" measure.
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ);
		return s.macro * m_impl->vertScale();
	}

	float TerrainGenV3::sampleHeight(double worldX, double worldZ) const
	{
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ);
		if (!s.valid)
			return m_impl->cfg.seaLevel;
		return m_impl->cfg.seaLevel + s.elev * m_impl->vertScale()
			+ m_impl->detail(worldX, worldZ, s.slope);
	}

	float TerrainGenV3::sampleWaterHeight(double, double) const
	{
		// The model's elevation is already relative to sea level, so the sea is exactly seaLevel.
		// (Elevated water — lakes, rivers — is not modelled yet.)
		return m_impl->cfg.seaLevel;
	}

	float TerrainGenV3::sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const
	{
		outWaterLevel = m_impl->cfg.seaLevel;
		return sampleHeight(worldX, worldZ);
	}

	float TerrainGenV3::sampleTemperature(double worldX, double worldZ) const
	{
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ);
		if (!s.valid)
			return 15.0f;
		return m_impl->temperatureFrom(s, m_impl->detail(worldX, worldZ, s.slope));
	}

	float TerrainGenV3::sampleHumidity(double worldX, double worldZ) const
	{
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ);
		if (!s.valid)
			return 0.5f;
		return m_impl->humidityOf(s);
	}

	float TerrainGenV3::sampleFogThickness(double worldX, double worldZ) const
	{
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ);
		if (!s.valid)
			return 0.0f;
		return m_impl->fogThicknessOf(s);
	}

	float TerrainGenV3::sampleFogHeightFalloff(double worldX, double worldZ) const
	{
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ);
		if (!s.valid)
			return 1.0f;
		// Uses the SHAPED temperature so the fog agrees with the biome splatting rather than the raw model.
		return m_impl->fogFalloffOf(m_impl->temperatureFrom(s, m_impl->detail(worldX, worldZ, s.slope)));
	}

	void TerrainGenV3::samplePoint(double worldX, double worldZ, TerrainPoint& out, ESampleDetail detail) const
	{
		const bool coarse = (detail == ESampleDetail::Coarse);
		const Impl::Sample s = m_impl->sampleField(worldX, worldZ, coarse);
		m_impl->fill(worldX, worldZ, s, /*withDetail*/ !coarse, out);
	}

	void TerrainGenV3::sampleGrid(double originX, double originZ, double step, uint32 resX, uint32 resZ,
	                              std::span<TerrainPoint> out, ESampleDetail detail) const
	{
		assert(out.size() >= (size_t)resX * resZ);
		if (resX == 0 || resZ == 0)
			return;

		// THE reason this override exists: resolve every tile the grid touches ONCE (one shared-cache lock
		// each), then fill without taking a lock at all. The base class's per-point loop meant a fetch —
		// mutex + LRU touch — for every one of the ~262k texels in a cascade, all of it contending with the
		// mesh worker for a grid that covers only a couple of dozen tiles.
		const bool coarse = (detail == ESampleDetail::Coarse);
		const double x1 = originX + step * (double)(resX - 1);
		const double z1 = originZ + step * (double)(resZ - 1);

		Impl::TileBlock block;
		if (!m_impl->resolveBlock(originX, originZ, x1, z1, coarse, block))
		{
			// Models not up yet: a flat sea-level world. Callers gate geometry on isReady(), so this only
			// shows up in bakes that raced the load.
			for (uint32 j = 0; j < resZ; j++)
				for (uint32 i = 0; i < resX; i++)
					m_impl->fill(0.0, 0.0, Impl::Sample{}, false, out[(size_t)j * resX + i]);
			return;
		}

		for (uint32 j = 0; j < resZ; j++)
		{
			const double wz = originZ + step * (double)j;
			for (uint32 i = 0; i < resX; i++)
			{
				const double wx = originX + step * (double)i;
				const Impl::Sample s = m_impl->sampleFromBlock(block, wx, wz);
				m_impl->fill(wx, wz, s, /*withDetail*/ !coarse, out[(size_t)j * resX + i]);
			}
		}
	}
}
