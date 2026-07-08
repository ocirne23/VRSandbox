module Procedural;

import Core;
import Core.glm;
import :Biome;

namespace Procedural
{
	EBiome classifyBiome(float temperature, float humidity, float altitude, float seaLevel, float slope01)
	{
		const float a = altitude - seaLevel; // height above sea level (meters)

		if (a < 0.0f)
			return EBiome::Ocean;
		if (a < 1.5f)
			return EBiome::Beach;

		// Steep faces are bare rock regardless of climate.
		if (slope01 > 0.62f)
			return EBiome::Rock;

		// High + cold -> permanent snow; high (any temp) -> exposed rock.
		if (a > 150.0f && temperature < 0.4f)
			return EBiome::Snow;
		if (a > 200.0f)
			return EBiome::Rock;

		// Whittaker temperature/humidity table.
		if (temperature < 0.2f)
			return humidity < 0.4f ? EBiome::Tundra : EBiome::Snow;
		if (temperature < 0.45f)
			return humidity < 0.4f ? EBiome::Tundra : EBiome::Taiga;
		if (temperature < 0.7f)
		{
			if (humidity < 0.25f) return EBiome::Grassland;
			if (humidity < 0.6f)  return EBiome::Forest;
			return EBiome::Rainforest;
		}
		// Hot
		if (humidity < 0.2f)  return EBiome::Desert;
		if (humidity < 0.5f)  return EBiome::Savanna;
		if (humidity < 0.75f) return EBiome::Grassland;
		return EBiome::Rainforest;
	}

	glm::vec3 biomeColor(EBiome biome)
	{
		switch (biome)
		{
		case EBiome::Ocean:      return glm::vec3(0.10f, 0.26f, 0.48f);
		case EBiome::Beach:      return glm::vec3(0.82f, 0.76f, 0.56f);
		case EBiome::Desert:     return glm::vec3(0.85f, 0.77f, 0.48f);
		case EBiome::Savanna:    return glm::vec3(0.66f, 0.63f, 0.34f);
		case EBiome::Grassland:  return glm::vec3(0.42f, 0.61f, 0.30f);
		case EBiome::Forest:     return glm::vec3(0.24f, 0.44f, 0.22f);
		case EBiome::Rainforest: return glm::vec3(0.13f, 0.36f, 0.18f);
		case EBiome::Taiga:      return glm::vec3(0.30f, 0.43f, 0.34f);
		case EBiome::Tundra:     return glm::vec3(0.55f, 0.56f, 0.50f);
		case EBiome::Snow:       return glm::vec3(0.92f, 0.94f, 0.97f);
		case EBiome::Rock:       return glm::vec3(0.42f, 0.40f, 0.38f);
		default:                 return glm::vec3(1.0f, 0.0f, 1.0f);
		}
	}
}
