module Procedural;

import Core;
import Core.glm;
import :Climate;
import :Noise;

namespace Procedural
{
	ClimateMaps::ClimateMaps(const TerrainConfig& cfg)
		: m_cfg(cfg)
		, m_base(cfg.seed + 0u)
		, m_continent(cfg.seed + 101u)
		, m_mountain(cfg.seed + 211u)
		, m_detail(cfg.seed + 331u)
		, m_warp(cfg.seed + 443u)
		, m_temperature(cfg.seed + 557u)
		, m_humidity(cfg.seed + 673u)
	{
	}

	float ClimateMaps::sampleHeight(double worldX, double worldZ) const
	{
		// Domain warp the base/mountain sample position for natural, non-grid-aligned features.
		const float warpX = m_warp.fbm((float)(worldX * m_cfg.continentFrequency * 0.5), (float)(worldZ * m_cfg.continentFrequency * 0.5), 3);
		const float warpZ = m_warp.fbm((float)(worldX * m_cfg.continentFrequency * 0.5 + 31.4), (float)(worldZ * m_cfg.continentFrequency * 0.5 + 27.1), 3);
		const double swx = worldX + (double)warpX * m_cfg.warpStrength;
		const double swz = worldZ + (double)warpZ * m_cfg.warpStrength;

		const float continent = m_base.fbm((float)(swx * m_cfg.continentFrequency), (float)(swz * m_cfg.continentFrequency), m_cfg.continentOctaves);

		// Continentalness mask in [0,1]: mountains only rise where this is high, so ranges cluster.
		const float contRaw = m_continent.fbm((float)(worldX * m_cfg.continentFrequency * 0.35), (float)(worldZ * m_cfg.continentFrequency * 0.35), 3);
		const float mask = glm::smoothstep(0.15f, 0.75f, contRaw * 0.5f + 0.5f);

		const float mountains = m_mountain.ridged((float)(swx * m_cfg.mountainFrequency), (float)(swz * m_cfg.mountainFrequency), m_cfg.mountainOctaves);
		const float detail = m_detail.fbm((float)(worldX * m_cfg.detailFrequency), (float)(worldZ * m_cfg.detailFrequency), m_cfg.detailOctaves);

		float h = m_cfg.seaLevel;
		h += continent * m_cfg.continentAmplitude;
		h += mountains * m_cfg.mountainAmplitude * mask;
		h += detail * m_cfg.detailAmplitude;
		return h;
	}

	float ClimateMaps::sampleTemperature(double worldX, double worldZ) const
	{
		const float base = m_temperature.fbm((float)(worldX * m_cfg.climateFrequency), (float)(worldZ * m_cfg.climateFrequency), 4) * 0.5f + 0.5f;
		const float altitude = sampleHeight(worldX, worldZ) - m_cfg.seaLevel;
		const float t = base - glm::max(0.0f, altitude) * m_cfg.lapseRate;
		return glm::clamp(t, 0.0f, 1.0f);
	}

	float ClimateMaps::sampleHumidity(double worldX, double worldZ) const
	{
		const float base = m_humidity.fbm((float)(worldX * m_cfg.climateFrequency * 0.8 + 13.7), (float)(worldZ * m_cfg.climateFrequency * 0.8 + 7.1), 4) * 0.5f + 0.5f;
		return glm::clamp(base, 0.0f, 1.0f);
	}
}
