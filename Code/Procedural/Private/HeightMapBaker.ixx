export module Procedural:HeightMapBaker;

import Core;
import Core.glm;
import :TerrainSampler;

export namespace Procedural
{
	// Sea level everywhere is a lie the ocean believes. A generator that models no lakes (V3) reports the
	// ocean's level at every point on the planet, so every scrap of terrain within a metre of it — an
	// inland hollow, a river flat, anything — reads as shoreline and the ocean runs swash up it.
	//
	// The swash already has the right gate: it fades out where the baked water level departs from sea
	// level, which is how landlocked water (lakes at altitude) is meant to be excluded. So the fix is not a
	// new rule, it is telling the truth — drop the water level far below any ground the ocean cannot
	// actually reach, and the existing gate does the rest. The terrain shader's beach overlay keys on the
	// same field, so inland sand goes with it.
	//
	// "Can reach" is a DISTANCE question, and this is the only place it is cheap to answer: the bake owns
	// the whole camera-centred height grid, so one chamfer pass gives every texel its distance to real
	// ocean in world metres. A per-point generator query cannot do this — it would have to search its own
	// field per sample — and the coarse detail level has no fine elevation to search at all.
	//
	// Both cascades measure the same world distance (just at their own texel size), so near and far agree.
	//
	// This applies to SUBMERGED ground as much as to dry land: a plateau at -0.05 m is under water by
	// definition, so a land-only rule could never drain one, and those are exactly what V3 produces —
	// kilometres of sea-level film with no ocean within reach of it. The clipmap rides the baked level
	// (oceanWaterOffset), so sinking it here actually removes the water rather than merely muting its swash.
	// A real shelving bay is kept wet by the RADIUS instead: its apron is too shallow to seed itself, but it
	// sits close to water that does. Radius shorter than the apron = the sea retreats off it.
	//
	// LAKES are never touched: a lake's surface already differs from sea level, so V2's lakes pass through
	// and keep their own shoreline.
	struct WaterReach
	{
		float radius = 4.0f;   // world metres of ocean proximity that still counts as shore
		float feather = 70.0f;  // world metres over which the drop eases in (no hard line on a beach)
		float drop = 2.0f;      // how far below sea level to sink unreachable water. Must clear the swash
		                        // gate's 1 m fade with room to spare; also pushes the beach overlay off.
		// How deep water must be to count as OCEAN and seed reach for the ground around it. Without it any
		// texel a hair under sea level qualifies, so one shallow inland dip vouches for every hollow within
		// the radius of it — the thing this whole pass exists to stop, reintroduced by a puddle. Swell needs
		// a real body of water behind it; this is where that line is drawn. 0 = any water below sea level.
		float swashDepth = 1.0f;

		// Compared to decide whether a baked map went stale (HeightMapBaker::update). Defaulted rather than
		// hand-written so a field added above is covered without anyone remembering to extend it — a missed
		// one would silently leave the tweak doing nothing until the camera happened to move far enough.
		bool operator==(const WaterReach&) const = default;
	};

	// Sinks the water level of texels that no ocean reaches. `texels` is one cascade, res*res*channels with
	// height at [0] and water level at [1]; a chamfer distance transform over the ocean texels drives it.
	inline void applyWaterReach(float* texels, uint32 res, uint32 channels, double texelSize,
	                            float seaLevel, const WaterReach& cfg)
	{
		if (channels < 2 || cfg.drop <= 0.0f || cfg.radius <= 0.0f)
			return;
		const size_t n = (size_t)res * res;
		// Seed: a texel is OCEAN if it is submerged BY AT LEAST swashDepth of water sitting at sea level.
		// Two tests, each load-bearing. The water-level one keeps a lake from seeding reach for its own
		// shore (and from rescuing the plain around it). The depth one keeps a puddle from doing the same:
		// terrain a centimetre under sea level is not something swell arrives from.
		constexpr float kSeaEps = 0.05f; // matches the shader's seaFade toe (ocean_wave.inc.glsl)
		const float seedLevel = seaLevel - std::max(cfg.swashDepth, 0.0f);
		std::vector<float> dist(n, FLT_MAX);
		for (size_t i = 0; i < n; ++i)
		{
			const float* t = texels + i * channels;
			if (t[0] <= seedLevel && std::abs(t[1] - seaLevel) <= kSeaEps)
				dist[i] = 0.0f;
		}

		// Two-pass chamfer. Measured worst case ~8% over true Euclidean, so the reach boundary can sit up to
		// ~8% of the radius off where a true Euclidean one would; keep the feather wider than that and it
		// never shows. Distances are in TEXELS here and converted to world metres once at the end.
		const float d1 = 1.0f, d2 = 1.41421356f;
		const auto relax = [&](size_t idx, size_t from, float w)
		{
			const float v = dist[from];
			if (v != FLT_MAX && v + w < dist[idx])
				dist[idx] = v + w;
		};
		for (uint32 j = 0; j < res; ++j)
			for (uint32 i = 0; i < res; ++i)
			{
				const size_t k = (size_t)j * res + i;
				if (j > 0)             relax(k, k - res, d1);
				if (i > 0)             relax(k, k - 1, d1);
				if (j > 0 && i > 0)    relax(k, k - res - 1, d2);
				if (j > 0 && i + 1 < res) relax(k, k - res + 1, d2);
			}
		for (uint32 j = res; j-- > 0; )
			for (uint32 i = res; i-- > 0; )
			{
				const size_t k = (size_t)j * res + i;
				if (j + 1 < res)              relax(k, k + res, d1);
				if (i + 1 < res)              relax(k, k + 1, d1);
				if (j + 1 < res && i + 1 < res) relax(k, k + res + 1, d2);
				if (j + 1 < res && i > 0)      relax(k, k + res - 1, d2);
			}

		const float r0 = cfg.radius;
		const float r1 = cfg.radius + std::max(cfg.feather, 1.0f);
		for (size_t i = 0; i < n; ++i)
		{
			float* t = texels + i * channels;
			if (std::abs(t[1] - seaLevel) > kSeaEps) // not the ocean's level: a lake, leave it alone
				continue;
			// SUBMERGED GROUND IS DROPPED TOO, deliberately. It is the whole point: a plateau sitting at
			// -0.05 m is under water by definition, so a land-only rule can never take its water away, and
			// V3 grows plenty of them — vast films of sea, kilometres inland, that no swell has ever reached.
			// The ocean's clipmap rides the baked level (oceanWaterOffset = level - sea level), so sinking it
			// here genuinely drains them rather than just muting their swash.
			//
			// What protects a REAL shelving bay is the reach RADIUS, not a land test: its apron is shallow
			// (so it never seeds itself) but it is close to water that does, and stays wet on that basis.
			// Set the radius shorter than the apron and the sea WILL retreat off it — that is the knob, and
			// it is the same trade in both directions.
			//
			// Sagging is the risk to watch, and the reason a blanket land-burial was removed from this bake
			// once before: the clipmap's coarse outer rings vertex-sample the level, and a triangle bridging
			// flat sea to a dropped texel tilts the surface. The feather is what keeps that a gentle ramp
			// instead of a cliff — and it only ever happens out past the radius, where the water is meant to
			// be leaving anyway.

			// No ocean anywhere in this map reads as FLT_MAX -> fully dropped, which is the right answer for
			// a map that is all inland. A texel whose nearest ocean lies just outside the map is wrongly
			// dropped, but that is the map's outer edge, and the near cascade crossfades to the far one
			// (which sees much further) exactly there.
			const float dWorld = dist[i] == FLT_MAX ? FLT_MAX : dist[i] * (float)texelSize;
			const float k = glm::clamp((dWorld - r0) / (r1 - r0), 0.0f, 1.0f);
			t[1] = seaLevel - cfg.drop * (k * k * (3.0f - 2.0f * k));
		}
	}

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
			const glm::vec2& camXZ, glm::vec2 ranges, uint32 res, uint32 numCascades, uint32 channels = 1,
			bool shoreLayout = false, const WaterReach* waterReach = nullptr)
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
					m_activeReach = m_pendingReach;
					m_activeReachOn = m_pendingReachOn;
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
			// The reach rule is BAKED INTO the water-level channel, so changing it invalidates the map the
			// same way a new sampler or range does. Without this the tweaks appear dead: nothing re-bakes
			// until the camera drifts a quarter of a range, and a slider you have to fly away from to test
			// is worse than no slider.
			const WaterReach reachNow = waterReach ? *waterReach : WaterReach{};
			const bool reachOnNow = waterReach != nullptr;
			const glm::vec2 drift = glm::abs(camXZ - m_activeCenter);
			const bool stale = !m_valid
				|| m_activeMaps != maps.get()
				|| m_activeRanges != ranges
				|| m_activeReachOn != reachOnNow
				|| (reachOnNow && !(m_activeReach == reachNow))
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
			m_pendingReach = reachNow;
			m_pendingReachOn = reachOnNow;
			const WaterReach reach = reachNow;
			const bool applyReach = reachOnNow;
			m_future = std::async(std::launch::async, [maps, center, ranges, res, numCascades, channels, shoreLayout, reach, applyReach]() {
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
									// 4x8-bit climate pack: fog thickness [0,1] | FREE | SEA-LEVEL TEMPERATURE
									// (TEMPERATURE_MIN_C..MAX_C) | humidity [0,1] (0 -> 0.0, 255 -> 1.0
									// exactly). 32 bits exceed
									// float32's exact-integer range, so the bits are BIT-CAST into the texel
									// (std::bit_cast here, floatBitsToUint in terrain_height.inc.glsl). The
									// whole path — vector moves, staging memcpy, copyBufferToImage, texelFetch
									// — carries raw bits with NO float arithmetic; some packs form NaN bit
									// patterns, which any arithmetic (the old float(packed)/+0.5 decode) would
									// corrupt. Keep it bit-exact end to end.
									// Temperature is stored as the SEA-LEVEL BASELINE, not as a sample: a sample
									// is only valid at the height it was taken from, and the two cascades bake
									// different heights for the same spot — the near one the full-detail
									// surface, the far one its 7.68 km average, which cannot know a peak
									// exists. Baking samples left the far cascade reporting the temperature of
									// the plateau a peak stands on, 10.6 C too warm, and its texture flipped at
									// the crossfade. A baseline has no anchor to disagree about: consumers
									// evaluate it at the height they shade (terrainTemperatureAt), against the
									// generator's one published lapse rate (u_terrainParams.y).
									// Bits 8-15 are FREE. They held the fog height-falloff, which was pure
									// redundancy (a function of temperature, so the shaders recompute it), then
									// briefly a per-texel lapse rate, which measured as redundant too — both
									// cascades regress the same data, so their rates agreed and removing it left
									// near-vs-far disagreement unchanged at 0.15 C.
									const auto q8 = [](float f) { return (uint32)(glm::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
									const uint32 packed = q8(p.fogThickness)
										// bits 8-15 free
										| (q8(temperatureTo01(p.temperatureSeaLevel)) << 16)
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

					// ... with ONE exception, and it is the reverse case: water the ocean cannot REACH. See
					// applyWaterReach. Done here, after the grid exists, because it is a distance question.
					if (applyReach)
						applyWaterReach(dst, res, channels, texelSize, maps->seaLevel(), reach);
				}
				return heights;
			});
			return shipped;
		}

		bool hasActiveMap() const { return m_valid; }

	private:
		std::future<std::vector<float>> m_future;
		WaterReach m_activeReach;      // the reach rule baked into the live map (see update)
		bool m_activeReachOn = false;
		WaterReach m_pendingReach;     // ... and the one the in-flight bake is using
		bool m_pendingReachOn = false;
		glm::vec2 m_pendingCenter = glm::vec2(0.0f); // inputs of the IN-FLIGHT bake
		glm::vec2 m_pendingRanges = glm::vec2(0.0f);
		const ITerrainSampler* m_pendingMaps = nullptr;  // identity only (never dereferenced)
		glm::vec2 m_activeCenter = glm::vec2(0.0f);  // inputs of the ACTIVE (shipped) map
		glm::vec2 m_activeRanges = glm::vec2(0.0f);
		const ITerrainSampler* m_activeMaps = nullptr;   // identity only (never dereferenced)
		bool m_valid = false;
	};
}
