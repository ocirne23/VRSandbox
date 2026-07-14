export module Procedural:HeightMapBaker;

import Core;
import Core.glm;
import :TerrainSampler;

export namespace Procedural
{
	// Async single-bake driver for camera-centered terrain height snapshots: res^2 floats of raw surface
	// height (world Y, m) per cascade, cascade-major, sampled from any ITerrainSampler. Shared by the
	// ocean shore map (1 cascade) and the fog terrain height map (2 cascades); both ship the result to a
	// renderer-side BakedWorldMap. One bake in flight at a time; a bake pins its sampler via the
	// captured shared_ptr, and the active map keeps working until the replacement lands. Re-bakes when the
	// maps identity or the ranges change, or the camera strays a quarter of the FINEST range from the
	// active center; centers snap to the COARSEST cascade's texel lattice so no cascade's features ever
	// swim between re-bakes.
	class HeightMapBaker
	{
	public:
		struct Baked
		{
			std::vector<float> texels;
			glm::vec2 center = glm::vec2(0.0f);
			glm::vec2 ranges = glm::vec2(0.0f); // world size per cascade (y = x when single-cascade)
		};

		// Polls the in-flight bake and starts a new one when the active map went stale. Returns true when
		// a completed bake should ship to the renderer (out is filled). active = false drops any finished
		// bake and invalidates (the caller clears its renderer-side map); maps must be non-null while
		// active. numCascades <= 2; ranges.y is ignored when numCascades == 1. channels (interleaved per
		// texel): 1 = terrain height; 2 = height + water level; 4 = four-channel layouts (4 not 3: RGB32F
		// is not a sampled-image format on most GPUs):
		//   shoreLayout = false (fog terrain cascades): (height, water level, PACKED fog thickness | fog
		//     height-falloff mul | temperature | humidity 4x8 bits BIT-CAST into the float texel, MACRO
		//     ALTITUDE). The packed channel tolerates no bilinear filtering and no float arithmetic (NaN
		//     patterns possible) — shaders decode texels via floatBitsToUint (terrainClimateAt); altitude
		//     is a plain float (bilinear-safe).
		//   shoreLayout = true (ocean shore map): (height, water level, flow-direction 8 bits — 0 = none,
		//     1..255 = angle/2pi — read nearest for the wave-direction rotation, spare).
		bool update(Baked& out, bool active, const std::shared_ptr<const ITerrainSampler>& maps,
			const glm::vec2& camXZ, glm::vec2 ranges, uint32 res, uint32 numCascades, uint32 channels = 1, bool shoreLayout = false)
		{
			bool shipped = false;
			if (m_future.valid() && m_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
			{
				std::vector<float> texels = m_future.get();
				if (active) // a bake finishing after its consumer got disabled is dropped
				{
					out.texels = std::move(texels);
					out.center = m_pendingCenter;
					out.ranges = m_pendingRanges;
					m_activeCenter = m_pendingCenter;
					m_activeRanges = m_pendingRanges;
					m_activeMaps = m_pendingMaps;
					m_valid = true;
					shipped = true;
				}
			}
			if (!active)
			{
				m_valid = false;
				return false;
			}
			if (m_future.valid())
				return shipped; // one bake in flight at a time

			ranges.x = glm::max(ranges.x, 1.0f);
			ranges.y = numCascades > 1 ? glm::max(ranges.y, ranges.x) : ranges.x;
			const glm::vec2 drift = glm::abs(camXZ - m_activeCenter);
			const bool stale = !m_valid
				|| m_activeMaps != maps.get()
				|| m_activeRanges != ranges
				|| glm::max(drift.x, drift.y) > ranges.x * 0.25f;
			if (!stale)
				return shipped;

			// Snap the shared center to the COARSEST cascade's texel lattice: every cascade's texels then
			// re-land on the exact same world positions bake after bake (with the default range ratio the
			// finer lattice divides the coarser), so re-bakes reproduce identical values where the terrain
			// is unchanged. Snapping to the finest lattice instead let the coarse cascade's texels shift
			// sub-coarse-texel per bake — on steep coasts a 16 m texel's height then jumped meters between
			// bakes, and consumers thresholding the field (the ocean land cull) flipped visibly.
			const float texel = ranges[numCascades > 1 ? 1 : 0] / float(res);
			const glm::vec2 center = glm::floor(camXZ / texel + 0.5f) * texel;
			m_pendingCenter = center;
			m_pendingRanges = ranges;
			m_pendingMaps = maps.get();
			m_future = std::async(std::launch::async, [maps, center, ranges, res, numCascades, channels, shoreLayout]() {
				std::vector<float> heights((size_t)numCascades * res * res * channels);
				std::vector<TerrainPoint> points; // reused across cascades
				for (uint32 c = 0; c < numCascades; ++c)
				{
					float* dst = heights.data() + (size_t)c * res * res * channels;
					const double range = (double)ranges[(int)c];
					const double texelSize = range / (double)res;
					const double x0 = (double)center.x - range * 0.5 + texelSize * 0.5; // texel centers
					const double z0 = (double)center.y - range * 0.5 + texelSize * 0.5;
					// Cascade 0 is the near/fine map; every later cascade spans tens of km at the same texel
					// count, so ask for the cheap approximation there. For a generator with a real per-point
					// cost (V3) that is the difference between a handful of coarse tiles and thousands of
					// full-detail ones covering terrain mostly beyond the mesh ring — see ESampleDetail. The
					// shader crossfades near->far, so the fidelity step blends in instead of seaming.
					const ESampleDetail detail = (c == 0) ? ESampleDetail::Full : ESampleDetail::Coarse;

					// ONE grid call per cascade, not res*res point calls: it lets the sampler resolve
					// whatever it needs (V3: its tile set, under one lock each) before touching a texel.
					points.resize((size_t)res * res);
					maps->sampleGrid(x0, z0, texelSize, res, res, points, detail);

					for (uint32 j = 0; j < res; ++j)
						for (uint32 i = 0; i < res; ++i)
						{
							const TerrainPoint& p = points[(size_t)j * res + i];
							float* texel = dst + ((size_t)j * res + i) * channels;
							texel[0] = p.height;
							if (channels > 1)
								texel[1] = p.waterLevel;
							if (channels > 3)
							{
								if (shoreLayout)
								{
									const float flow = p.flowAngle01;
									texel[2] = flow < 0.0f ? 0.0f : (float)(1u + (uint32)(glm::clamp(flow, 0.0f, 1.0f) * 254.0f + 0.5f));
									texel[3] = 0.0f; // spare
								}
								else
								{
									// 4x8-bit climate pack: fog thickness [0,1] | fog height-falloff mul
									// (0..FOG_FALLOFF_MUL_MAX, 1 = neutral) | temperature (TEMPERATURE_MIN_C..
									// MAX_C) | humidity [0,1] (0 -> 0.0, 255 -> 1.0 exactly). 32 bits exceed
									// float32's exact-integer range, so the bits are BIT-CAST into the texel
									// (std::bit_cast here, floatBitsToUint in terrain_height.inc.glsl). The
									// whole path — vector moves, staging memcpy, copyBufferToImage, texelFetch
									// — carries raw bits with NO float arithmetic; some packs form NaN bit
									// patterns, which any arithmetic (the old float(packed)/+0.5 decode) would
									// corrupt. Keep it bit-exact end to end.
									const auto q8 = [](float f) { return (uint32)(glm::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
									const uint32 packed = q8(p.fogThickness)
										| (q8(p.fogFalloffMul * (1.0f / FOG_FALLOFF_MUL_MAX)) << 8)
										| (q8(temperatureTo01(p.temperature)) << 16)
										| (q8(p.humidity) << 24);
									texel[2] = std::bit_cast<float>(packed);
									texel[3] = p.altitude; // macro elevation (terrain coloring)
								}
							}
						}
					// NOTE: the water-level channel bakes EXACTLY what the sampler reports — no post-processing.
					// A land-burial dive (sinking the level under land texels against standard-Z depth noise)
					// briefly lived here and backfired: the ocean clipmap's coarse outer rings vertex-sample
					// this channel, and one triangle bridging a flat water texel to a deeply dived land texel
					// visibly sagged the far sea near coasts. Reversed-Z made any such separation unnecessary.
				}
				return heights;
			});
			return shipped;
		}

		bool hasActiveMap() const { return m_valid; }

	private:
		std::future<std::vector<float>> m_future;
		glm::vec2 m_pendingCenter = glm::vec2(0.0f); // inputs of the IN-FLIGHT bake
		glm::vec2 m_pendingRanges = glm::vec2(0.0f);
		const ITerrainSampler* m_pendingMaps = nullptr;  // identity only (never dereferenced)
		glm::vec2 m_activeCenter = glm::vec2(0.0f);  // inputs of the ACTIVE (shipped) map
		glm::vec2 m_activeRanges = glm::vec2(0.0f);
		const ITerrainSampler* m_activeMaps = nullptr;   // identity only (never dereferenced)
		bool m_valid = false;
	};
}
