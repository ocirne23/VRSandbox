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

		// Lakes: a jittered lattice of nodes (point-evaluated drainage graph, after Génevaux et al. 2013 /
		// Gaillard et al. 2019 "Dendry"); nodes that are LOCAL MINIMA among their 8 neighbors are genuine
		// depressions, and a hash-picked fraction of them holds a lake — a basin carved below the node's
		// level and filled flat (terrain clips the shoreline).
		float  networkCellSize = 1200.0f; // lattice spacing (m): smaller = more (and smaller) lakes
		float  lakeCoverage = 0.5f;  // fraction of lattice local-minima that hold a lake (0 disables)
		float  lakeDepth = 8.0f;     // lake basin carve depth below its surface (m); 0 disables

		// Regional fog thickness: a [0,1] multiplier field on the volumetric fog's density (foggy regions
		// vs clear ones), baked into the fog terrain cascades and applied by "Fog/Region strength".
		float  fogFrequency = 0.0003f;
		float  fogCoverage = 0.5f; // 0..1 fraction of the world that is foggy (0 = clear everywhere)
	};

	class ClimateMaps
	{
	public:
		explicit ClimateMaps(const TerrainConfig& cfg);

		// Surface height in world meters (Y). Continuous everywhere.
		float sampleHeight(double worldX, double worldZ) const;
		// Water surface height in world meters (Y): seaLevel over the open ocean, the lake level over lake
		// basins. Water is present wherever this exceeds sampleHeight; outside features it drops steeply
		// below ground (hidden — depth buffer clips it).
		float sampleWaterHeight(double worldX, double worldZ) const;
		// Both fields from one evaluation (the terrain/water math is shared; bakes sampling both per texel
		// pay half the noise cost this way). Returns the terrain height, writes the water level.
		float sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const;
		// Regional fog thickness multiplier [0,1] (see TerrainConfig::fogCoverage).
		float sampleFogThickness(double worldX, double worldZ) const;
		// Normalized [0,1] temperature (cold 0 .. hot 1); drops with altitude.
		float sampleTemperature(double worldX, double worldZ) const;
		// Normalized [0,1] humidity (dry 0 .. wet 1).
		float sampleHumidity(double worldX, double worldZ) const;

		const TerrainConfig& config() const { return m_cfg; }

	private:
		// Shared terrain + water evaluation; outWaterLevel may be null (terrain-only callers skip nothing —
		// the hydrology carve needs the same network — but skip the water-level max chain).
		float computeSurface(double worldX, double worldZ, float* outWaterLevel) const;
		glm::dvec2 hydroNodePos(int32 cellX, int32 cellZ) const;  // jittered lattice node position
		float hydroNodeElev(int32 cellX, int32 cellZ) const;      // coarse terrain at the node (memoized)

		TerrainConfig m_cfg;
		uint32 m_instanceId = 0; // unique per ClimateMaps: keys the thread-local node-elevation cache
		NoiseField    m_base;       // continental fbm
		NoiseField    m_continent;  // continentalness mask
		NoiseField    m_mountain;   // ridged mountains
		NoiseField    m_detail;     // fine detail
		NoiseField    m_warp;       // domain warp
		NoiseField    m_temperature;
		NoiseField    m_humidity;
		NoiseField    m_fog;        // regional fog-thickness field
	};
}
