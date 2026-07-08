export module Procedural:TerrainChunk;

import Core;
import Core.glm;

export namespace Procedural
{
	// CPU output of one generated chunk. Geometry is chunk-LOCAL in X/Z (0..chunkSize) with world-space Y,
	// so a consumer places it with a pure XZ translation to the chunk origin (keeps float precision high).
	// SoA arrays mirror IMeshData's layout so the renderer bridge can hand them over without repacking.
	// A vertical skirt (border walls dropped by the configured depth) is appended after the surface grid to
	// hide cracks where neighbouring chunks meet at a coarser LOD. Surface color is deferred to a future
	// terrain shader (computed from height/biome on the GPU), so no per-chunk color texture is baked here.
	struct TerrainChunkMesh
	{
		std::vector<glm::vec3> positions;   // local X/Z, world Y
		std::vector<glm::vec3> normals;
		std::vector<glm::vec3> tangents;
		std::vector<glm::vec3> bitangents;
		std::vector<glm::vec3> texCoords;   // xy in [0,1] across the chunk, z unused
		std::vector<uint32>    indices;

		void clear()
		{
			positions.clear(); normals.clear(); tangents.clear();
			bitangents.clear(); texCoords.clear(); indices.clear();
		}
	};
}
