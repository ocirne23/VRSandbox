export module Procedural:Biome;

import Core;
import Core.glm;

export namespace Procedural
{
	enum class EBiome : uint8
	{
		Ocean,
		Beach,
		Desert,
		Savanna,
		Grassland,
		Forest,
		Rainforest,
		Taiga,
		Tundra,
		Snow,
		Rock,
		Count
	};

	// Whittaker-style classification from the climate maps. temperature/humidity are normalized [0,1];
	// altitude and seaLevel are in world meters (Y). slope01 in [0,1] (0 = flat, 1 = vertical) pushes
	// steep faces toward bare Rock. waterDepth (m) = local water surface - terrain (ClimateMaps::
	// sampleWaterHeight - sampleHeight): anything submerged classifies as Ocean, so lakes/rivers at
	// altitude read as water too. Pure and deterministic.
	EBiome classifyBiome(float temperature, float humidity, float altitude, float seaLevel, float slope01 = 0.0f, float waterDepth = 0.0f);

	// Approximate base albedo for a biome, linear-ish RGB in [0,1].
	glm::vec3 biomeColor(EBiome biome);
}
