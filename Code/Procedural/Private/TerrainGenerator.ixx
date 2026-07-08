export module Procedural:TerrainGenerator;

import Core;
import Core.glm;
import :Climate;
import :TerrainChunk;

export namespace Procedural
{
	struct ChunkParams
	{
		glm::ivec2 coord{ 0, 0 }; // chunk grid coordinate (world origin = coord * chunkSize on X/Z)
		uint32 lod = 0;           // 0 = finest; each level halves the grid resolution
		float  chunkSize = 128.0f;
		uint32 lod0Res = 64;      // quads per side at LOD0 (must be a power of two for clean LOD subsetting)
		float  skirtDepth = 8.0f; // how far border skirt walls drop below the surface
	};

	// Generates one chunk's surface mesh (geometry only). Pure and thread-safe given a shared ClimateMaps.
	// The height field is sampled in world space and the ocean surface is flattened to sea level, so oceans
	// render flat and heights agree across chunk/LOD boundaries.
	void generateChunk(const ClimateMaps& maps, const ChunkParams& params, TerrainChunkMesh& out);
}
