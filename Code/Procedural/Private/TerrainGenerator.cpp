module Procedural;

import Core;
import Core.glm;
import :TerrainGenerator;
import :Climate;
import :TerrainChunk;

namespace Procedural
{
	void generateChunk(const ClimateMaps& maps, const ChunkParams& params, TerrainChunkMesh& out)
	{
		out.clear();

		const uint32 res = glm::max(1u, params.lod0Res >> params.lod);
		const uint32 vpr = res + 1; // vertices per row
		const float  step = params.chunkSize / (float)res;
		const float  seaLevel = maps.config().seaLevel;
		const double ox = (double)params.coord.x * (double)params.chunkSize;
		const double oz = (double)params.coord.y * (double)params.chunkSize;

		// Surface height with the ocean flattened to sea level (flat water; color comes later from a shader).
		auto surface = [&](float localX, float localZ) -> float
		{
			const float h = maps.sampleHeight(ox + (double)localX, oz + (double)localZ);
			return glm::max(h, seaLevel);
		};

		out.positions.reserve((size_t)vpr * vpr);
		out.normals.reserve((size_t)vpr * vpr);
		out.tangents.reserve((size_t)vpr * vpr);
		out.bitangents.reserve((size_t)vpr * vpr);
		out.texCoords.reserve((size_t)vpr * vpr);

		const float eps = step * 0.5f; // finite-difference step for normals (half a cell)

		for (uint32 row = 0; row <= res; ++row)
		{
			for (uint32 col = 0; col <= res; ++col)
			{
				const float lx = (float)col * step;
				const float lz = (float)row * step;
				const float y = surface(lx, lz);

				// Central-difference TBN (matches the engine's existing terrain convention).
				const float hL = surface(lx - eps, lz);
				const float hR = surface(lx + eps, lz);
				const float hD = surface(lx, lz - eps);
				const float hU = surface(lx, lz + eps);
				const glm::vec3 dpdu(2.0f * eps, hR - hL, 0.0f);
				const glm::vec3 dpdv(0.0f, hU - hD, 2.0f * eps);
				const glm::vec3 normal = -glm::normalize(glm::cross(dpdu, dpdv));

				out.positions.push_back({ lx, y, lz });
				out.normals.push_back(normal);
				out.tangents.push_back(glm::normalize(dpdu));
				out.bitangents.push_back(glm::normalize(dpdv));
				out.texCoords.push_back({ (float)col / (float)res, (float)row / (float)res, 0.0f });
			}
		}

		out.indices.reserve((size_t)res * res * 6);
		for (uint32 row = 0; row < res; ++row)
		{
			for (uint32 col = 0; col < res; ++col)
			{
				const uint32 a = row * vpr + col;
				const uint32 b = a + 1;
				const uint32 c = (row + 1) * vpr + col;
				const uint32 d = c + 1;
				out.indices.push_back(a); out.indices.push_back(c); out.indices.push_back(b);
				out.indices.push_back(b); out.indices.push_back(c); out.indices.push_back(d);
			}
		}

		// --- Skirt: a vertical border wall dropped by skirtDepth, hiding cracks against coarser neighbours.
		// Emitted double-sided (both windings) so it fills the gap regardless of view/backface-cull direction;
		// the extra triangles are negligible and this keeps the generator neighbour-independent.
		if (params.skirtDepth > 0.0f)
		{
			// Perimeter vertex indices, walked as a closed loop.
			std::vector<uint32> perimeter;
			perimeter.reserve(4u * res);
			for (uint32 col = 0; col < res; ++col)           perimeter.push_back(0u * vpr + col);        // top row, L->R
			for (uint32 row = 0; row < res; ++row)           perimeter.push_back(row * vpr + res);        // right col, T->B
			for (uint32 col = res; col > 0; --col)           perimeter.push_back(res * vpr + col);        // bottom row, R->L
			for (uint32 row = res; row > 0; --row)           perimeter.push_back(row * vpr + 0u);         // left col, B->T

			for (size_t k = 0; k < perimeter.size(); ++k)
			{
				const uint32 iA = perimeter[k];
				const uint32 iB = perimeter[(k + 1) % perimeter.size()];

				const glm::vec3 lowA = out.positions[iA] - glm::vec3(0.0f, params.skirtDepth, 0.0f);
				const glm::vec3 lowB = out.positions[iB] - glm::vec3(0.0f, params.skirtDepth, 0.0f);

				const uint32 la = (uint32)out.positions.size();
				out.positions.push_back(lowA);
				out.normals.push_back(out.normals[iA]);
				out.tangents.push_back(out.tangents[iA]);
				out.bitangents.push_back(out.bitangents[iA]);
				out.texCoords.push_back(out.texCoords[iA]);

				const uint32 lb = (uint32)out.positions.size();
				out.positions.push_back(lowB);
				out.normals.push_back(out.normals[iB]);
				out.tangents.push_back(out.tangents[iB]);
				out.bitangents.push_back(out.bitangents[iB]);
				out.texCoords.push_back(out.texCoords[iB]);

				// Quad [iA, iB, lb, la] as two triangles, then reversed for the back side.
				out.indices.push_back(iA); out.indices.push_back(iB); out.indices.push_back(lb);
				out.indices.push_back(iA); out.indices.push_back(lb); out.indices.push_back(la);
				out.indices.push_back(iA); out.indices.push_back(lb); out.indices.push_back(iB);
				out.indices.push_back(iA); out.indices.push_back(la); out.indices.push_back(lb);
			}
		}
	}
}
