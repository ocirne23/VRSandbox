export module Procedural:HeightMapBaker;

import Core;
import Core.glm;
import :Climate;

export namespace Procedural
{
	// Async single-bake driver for camera-centered terrain height snapshots: res^2 floats of raw surface
	// height (world Y, m) per cascade, cascade-major, sampled from a ClimateMaps. Shared by the ocean
	// shore map (1 cascade) and the fog terrain height map (2 cascades); both ship the result to a
	// renderer-side BakedWorldMap. One bake in flight at a time; a bake pins its ClimateMaps via the
	// captured shared_ptr, and the active map keeps working until the replacement lands. Re-bakes when the
	// maps identity or the ranges change, or the camera strays a quarter of the FINEST range from the
	// active center; centers snap to the finest cascade's texel lattice so re-bakes never make features
	// swim sub-texel.
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
		// active. numCascades <= 2; ranges.y is ignored when numCascades == 1.
		bool update(Baked& out, bool active, const std::shared_ptr<const ClimateMaps>& maps,
			const glm::vec2& camXZ, glm::vec2 ranges, uint32 res, uint32 numCascades)
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

			const float texel = ranges.x / float(res);
			const glm::vec2 center = glm::floor(camXZ / texel + 0.5f) * texel;
			m_pendingCenter = center;
			m_pendingRanges = ranges;
			m_pendingMaps = maps.get();
			m_future = std::async(std::launch::async, [maps, center, ranges, res, numCascades]() {
				std::vector<float> heights((size_t)numCascades * res * res);
				for (uint32 c = 0; c < numCascades; ++c)
				{
					float* dst = heights.data() + (size_t)c * res * res;
					const double range = (double)ranges[(int)c];
					const double texelSize = range / (double)res;
					const double x0 = (double)center.x - range * 0.5 + texelSize * 0.5; // texel centers
					const double z0 = (double)center.y - range * 0.5 + texelSize * 0.5;
					for (uint32 j = 0; j < res; ++j)
						for (uint32 i = 0; i < res; ++i)
							dst[(size_t)j * res + i] = maps->sampleHeight(x0 + i * texelSize, z0 + j * texelSize);
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
		const ClimateMaps* m_pendingMaps = nullptr;  // identity only (never dereferenced)
		glm::vec2 m_activeCenter = glm::vec2(0.0f);  // inputs of the ACTIVE (shipped) map
		glm::vec2 m_activeRanges = glm::vec2(0.0f);
		const ClimateMaps* m_activeMaps = nullptr;   // identity only (never dereferenced)
		bool m_valid = false;
	};
}
