export module Procedural:Climate;

import Core;
import Core.glm;
import :Noise;

export namespace Procedural
{
	// All generation parameters. Frequencies are in cycles-per-meter (world units), amplitudes in meters.
	// A ClimateMaps built from one TerrainConfig is a pure function of world position, so every chunk and
	// every LOD that samples the same world coordinate gets the same result — this is what makes chunk and
	// LOD boundaries consistent.
	struct TerrainConfig
	{
		uint32 seed = 1337;
		float  seaLevel = 0.0f;   // world Y of the sea surface

		// Continental base (broad landmasses / rolling terrain)
		float  continentFrequency = 0.0009f;
		uint32 continentOctaves   = 5;
		float  continentAmplitude = 60.0f;

		// Mountains (ridged, gated by a continentalness mask so they cluster into ranges)
		float  mountainFrequency = 0.0025f;
		uint32 mountainOctaves   = 6;
		float  mountainAmplitude = 180.0f;

		// Fine surface detail
		float  detailFrequency = 0.02f;
		uint32 detailOctaves   = 4;
		float  detailAmplitude = 6.0f;

		// Domain warp applied to the base/mountain sample position (meters of displacement)
		float  warpStrength = 40.0f;

		// Climate fields
		float  climateFrequency = 0.00025f;
		float  lapseRate = 0.0018f; // normalized temperature drop per meter of altitude (snowy peaks)
	};

	class ClimateMaps
	{
	public:
		explicit ClimateMaps(const TerrainConfig& cfg);

		// Surface height in world meters (Y). Continuous everywhere.
		float sampleHeight(double worldX, double worldZ) const;
		// Normalized [0,1] temperature (cold 0 .. hot 1); drops with altitude.
		float sampleTemperature(double worldX, double worldZ) const;
		// Normalized [0,1] humidity (dry 0 .. wet 1).
		float sampleHumidity(double worldX, double worldZ) const;

		const TerrainConfig& config() const { return m_cfg; }

	private:
		TerrainConfig m_cfg;
		NoiseField    m_base;       // continental fbm
		NoiseField    m_continent;  // continentalness mask
		NoiseField    m_mountain;   // ridged mountains
		NoiseField    m_detail;     // fine detail
		NoiseField    m_warp;       // domain warp
		NoiseField    m_temperature;
		NoiseField    m_humidity;
	};
}
