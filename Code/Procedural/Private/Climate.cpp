module Procedural;

import Core;
import Core.glm;
import :Climate;
import :Noise;

namespace
{
	uint32 hashCellCoord(int32 x, int32 z, uint32 seed)
	{
		uint32 h = seed * 0x9E3779B9u;
		h ^= (uint32)x * 0x85EBCA6Bu;
		h ^= (uint32)z * 0xC2B2AE35u;
		h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
		return h;
	}
	float cellHash01(uint32 h) { return float(h & 0xFFFFFFu) * (1.0f / 16777215.0f); }

	std::atomic<uint32> s_climateInstanceCounter{ 0 };
}

namespace Procedural
{
	ClimateMaps::ClimateMaps(const TerrainConfig& cfg)
		: m_cfg(cfg)
		, m_instanceId(++s_climateInstanceCounter)
		, m_base(cfg.seed + 0u)
		, m_continent(cfg.seed + 101u)
		, m_mountain(cfg.seed + 211u)
		, m_detail(cfg.seed + 331u)
		, m_warp(cfg.seed + 443u)
		, m_temperature(cfg.seed + 557u)
		, m_humidity(cfg.seed + 673u)
		, m_fog(cfg.seed + 947u)
	{
	}

	glm::dvec2 ClimateMaps::hydroNodePos(int32 cellX, int32 cellZ) const
	{
		const uint32 h = hashCellCoord(cellX, cellZ, m_cfg.seed ^ 0xA5A5A5A5u);
		const float jx = cellHash01(h) - 0.5f;
		const float jz = cellHash01(h * 0x9E3779B9u + 1u) - 0.5f;
		const double cs = (double)glm::max(m_cfg.networkCellSize, 200.0f);
		return glm::dvec2(((double)cellX + 0.5 + (double)jx * 0.7) * cs, ((double)cellZ + 0.5 + (double)jz * 0.7) * cs);
	}

	// Coarse terrain elevation at a lattice node: the same continent/mountain structure computeSurface
	// builds on (minus warp and detail — the hydrology tolerates meters of mismatch because channels carve
	// against the REAL local height). Memoized per thread: a chunk evaluates ~50 distinct nodes but calls
	// this ~10^6 times through the 7x7 gathers.
	float ClimateMaps::hydroNodeElev(int32 cellX, int32 cellZ) const
	{
		struct Entry { uint32 id = 0; int32 cx = 0, cz = 0; float elev = 0.0f; };
		thread_local Entry cache[1024];
		Entry& e = cache[hashCellCoord(cellX, cellZ, m_instanceId) & 1023u];
		if (e.id == m_instanceId && e.cx == cellX && e.cz == cellZ)
			return e.elev;

		const glm::dvec2 p = hydroNodePos(cellX, cellZ);
		const float cont = m_base.fbmEroded((float)(p.x * m_cfg.continentFrequency), (float)(p.y * m_cfg.continentFrequency), 3);
		const float contRaw = m_continent.fbm((float)(p.x * m_cfg.continentFrequency * 0.35), (float)(p.y * m_cfg.continentFrequency * 0.35), 2);
		const float mask = glm::smoothstep(0.15f, 0.75f, contRaw * 0.5f + 0.5f);
		const float ridg = m_mountain.ridged((float)(p.x * m_cfg.mountainFrequency), (float)(p.y * m_cfg.mountainFrequency), 2);
		const float elev = m_cfg.seaLevel + cont * m_cfg.continentAmplitude + ridg * mask * m_cfg.mountainAmplitude;

		e = Entry{ m_instanceId, cellX, cellZ, elev };
		return elev;
	}

	float ClimateMaps::computeSurface(double worldX, double worldZ, float* outWaterLevel) const
	{
		// Domain warp the base/mountain sample position for natural, non-grid-aligned features.
		const float warpX = m_warp.fbm((float)(worldX * m_cfg.continentFrequency * 0.5), (float)(worldZ * m_cfg.continentFrequency * 0.5), 3);
		const float warpZ = m_warp.fbm((float)(worldX * m_cfg.continentFrequency * 0.5 + 31.4), (float)(worldZ * m_cfg.continentFrequency * 0.5 + 27.1), 3);
		const double swx = worldX + (double)warpX * m_cfg.warpStrength;
		const double swz = worldZ + (double)warpZ * m_cfg.warpStrength;

		// Slope-damped fBm (Musgrave/Quilez erosion look): smooth valley floors, craggy ridges.
		const float continent = m_base.fbmEroded((float)(swx * m_cfg.continentFrequency), (float)(swz * m_cfg.continentFrequency), m_cfg.continentOctaves);

		// Continentalness mask in [0,1]: mountains only rise where this is high, so ranges cluster.
		const float contRaw = m_continent.fbm((float)(worldX * m_cfg.continentFrequency * 0.35), (float)(worldZ * m_cfg.continentFrequency * 0.35), 3);
		const float mask = glm::smoothstep(0.15f, 0.75f, contRaw * 0.5f + 0.5f);

		const float mountains = m_mountain.ridged((float)(swx * m_cfg.mountainFrequency), (float)(swz * m_cfg.mountainFrequency), m_cfg.mountainOctaves);
		const float detail = m_detail.fbm((float)(worldX * m_cfg.detailFrequency), (float)(worldZ * m_cfg.detailFrequency), m_cfg.detailOctaves);

		// The low-frequency "base" terrain: what lake surfaces derive from (near-flat locally, tracks the
		// landscape's broad altitude), and what the full height builds on.
		const float base = m_cfg.seaLevel + continent * m_cfg.continentAmplitude;
		float h = base + mountains * m_cfg.mountainAmplitude * mask + detail * m_cfg.detailAmplitude;

		float water = m_cfg.seaLevel;
		// Outside a feature's water mask the level must dive below the terrain FAST (so no water shows)
		// but stay continuous — the surface wall this creates is underground, where the depth buffer
		// clips the ocean mesh against the terrain (the same mechanism as the coast waterline).
		constexpr float DROP = 30.0f;

		if (m_cfg.lakeDepth > 0.0f && m_cfg.lakeCoverage > 0.0f)
		{
			// Lake lattice: gather the 5x5 node neighborhood (jittered lattice; positions relative to the
			// query point so the geometry below is float-exact at planet scale). Nodes of the inner 3x3
			// that are LOCAL MINIMA among their 8 neighbors are genuine depressions and may hold a lake —
			// a lake's radius is under one cell, so only nodes adjacent to the query cell can reach it.
			const double cs = (double)glm::max(m_cfg.networkCellSize, 200.0f);
			const int32 ccx = (int32)std::floor(worldX / cs);
			const int32 ccz = (int32)std::floor(worldZ / cs);

			glm::vec2 npos[5][5];
			float nelev[5][5];
			for (int32 j = 0; j < 5; ++j)
				for (int32 i = 0; i < 5; ++i)
				{
					const int32 cx = ccx + i - 2, cz = ccz + j - 2;
					const glm::dvec2 p = hydroNodePos(cx, cz);
					npos[j][i] = glm::vec2((float)(p.x - worldX), (float)(p.y - worldZ));
					nelev[j][i] = hydroNodeElev(cx, cz);
				}

			for (int32 j = 1; j <= 3; ++j)
				for (int32 i = 1; i <= 3; ++i)
				{
					const float e0 = nelev[j][i];
					if (e0 <= m_cfg.seaLevel + 1.0f)
						continue;
					bool localMin = true;
					for (int32 dj = -1; dj <= 1 && localMin; ++dj)
						for (int32 di = -1; di <= 1; ++di)
							if (nelev[j + dj][i + di] < e0) { localMin = false; break; }
					if (!localMin)
						continue;
					// A hash decides whether this depression holds a lake, and its size.
					const uint32 hh = hashCellCoord(ccx + i - 2, ccz + j - 2, m_cfg.seed ^ 0x51ED270Bu);
					if (cellHash01(hh) >= m_cfg.lakeCoverage)
						continue;
					const float r = (float)cs * (0.2f + 0.25f * cellHash01(hh * 0x9E3779B9u + 7u));
					const float d = glm::length(npos[j][i]);
					if (d >= r * 1.7f)
						continue;
					const float level = e0 - 1.0f;
					const float mask2 = 1.0f - glm::smoothstep(0.4f * r, r, d);
					h = glm::mix(h, glm::min(h, level - m_cfg.lakeDepth), mask2); // basin carve
					if (outWaterLevel)
					{
						// FLAT water level held past the basin rim; the terrain cuts the shoreline (the
						// shore/depth handling stops water wherever ground rises above it), so the basin
						// fills to its actual shape. Never shape the water edge with its own falloff.
						const float wdrop = glm::smoothstep(r * 1.1f, r * 1.7f, d);
						water = glm::max(water, level - wdrop * DROP);
					}
				}
		}

		if (outWaterLevel)
			*outWaterLevel = water;
		return h;
	}

	float ClimateMaps::sampleHeight(double worldX, double worldZ) const
	{
		return computeSurface(worldX, worldZ, nullptr);
	}

	float ClimateMaps::sampleWaterHeight(double worldX, double worldZ) const
	{
		float water;
		computeSurface(worldX, worldZ, &water);
		return water;
	}

	float ClimateMaps::sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const
	{
		return computeSurface(worldX, worldZ, &outWaterLevel);
	}

	float ClimateMaps::sampleFogThickness(double worldX, double worldZ) const
	{
		if (m_cfg.fogCoverage <= 0.0f)
			return 0.0f;
		const float n = m_fog.fbm((float)(worldX * m_cfg.fogFrequency + 5.3), (float)(worldZ * m_cfg.fogFrequency + 17.9), 3) * 0.5f + 0.5f;
		// Soft threshold: `coverage` of the domain is foggy, with wide transition bands so density ramps
		// in and out over regions instead of switching at a contour.
		return glm::smoothstep(1.0f - m_cfg.fogCoverage - 0.2f, 1.0f - m_cfg.fogCoverage + 0.2f, n);
	}

	float ClimateMaps::sampleTemperature(double worldX, double worldZ) const
	{
		const float base = m_temperature.fbm((float)(worldX * m_cfg.climateFrequency), (float)(worldZ * m_cfg.climateFrequency), 4) * 0.5f + 0.5f;
		const float altitude = sampleHeight(worldX, worldZ) - m_cfg.seaLevel;
		const float t01 = glm::clamp(base - glm::max(0.0f, altitude) * m_cfg.lapseRate, 0.0f, 1.0f);
		return TEMPERATURE_MIN_C + t01 * (TEMPERATURE_MAX_C - TEMPERATURE_MIN_C); // sampler contract: Celsius
	}

	float ClimateMaps::sampleHumidity(double worldX, double worldZ) const
	{
		const float base = m_humidity.fbm((float)(worldX * m_cfg.climateFrequency * 0.8 + 13.7), (float)(worldZ * m_cfg.climateFrequency * 0.8 + 7.1), 4) * 0.5f + 0.5f;
		return glm::clamp(base, 0.0f, 1.0f);
	}
}
