export module RendererVK:Light;

import Core;
import Core.glm;

import :Layout;

export struct Light : RendererVKLayout::LightInfo
{

};

export struct LightGridTable
{
	std::vector<RendererVKLayout::LightGrid> grids;
	std::vector<RendererVKLayout::LightGridTableEntry> table;

	size_t getLightGridsByteSize() const
	{
		return sizeof(RendererVKLayout::LightGrid) * grids.size();
	}

	size_t getLightTableByteSize() const
	{
		return sizeof(RendererVKLayout::LightGridTableEntry) * table.size();
	}

	void initialize(uint16 tableSize)
	{
		table.resize(tableSize);
		memset(table.data(), 0xFF, sizeof(RendererVKLayout::LightGridTableEntry) * table.size());
	}

	void clear()
	{
		grids.clear();
		memset(table.data(), 0xFF, sizeof(RendererVKLayout::LightGridTableEntry) * table.size());
	}

	void addLight(const Light& light, uint16 lightIdx)
	{
		const glm::ivec3 lightPosMin = glm::ivec3(light.pos - glm::vec3(light.radius));
		const glm::ivec3 lightPosMax = glm::ivec3(light.pos + glm::vec3(light.radius));

		const glm::ivec3 gridPosMin = lightPosMin / glm::ivec3(RendererVKLayout::LIGHT_GRID_SIZE);
		const glm::ivec3 gridPosMax = lightPosMax / glm::ivec3(RendererVKLayout::LIGHT_GRID_SIZE);
		for (int gridX = gridPosMin.x; gridX <= gridPosMax.x; ++gridX)
		{
			for (int gridY = gridPosMin.y; gridY <= gridPosMax.y; ++gridY)
			{
				for (int gridZ = gridPosMin.z; gridZ <= gridPosMax.z; ++gridZ)
				{
					const glm::ivec3 gridPos = glm::ivec3(gridX, gridY, gridZ);
					RendererVKLayout::LightGrid* pGrid = getOrAddGrid(gridPos);
					if (!pGrid)
						continue;

					const glm::ivec3 minCell = glm::max(lightPosMin - pGrid->gridMin, glm::ivec3(0));
					const glm::ivec3 maxCell = glm::min(lightPosMax - pGrid->gridMin, glm::ivec3(RendererVKLayout::LIGHT_GRID_SIZE - 1));
					for (int cellX = minCell.x; cellX <= maxCell.x; ++cellX)
					{
						for (int cellY = minCell.y; cellY <= maxCell.y; ++cellY)
						{
							for (int cellZ = minCell.z; cellZ <= maxCell.z; ++cellZ)
							{
								RendererVKLayout::LightCell& cell = pGrid->cells[cellX + cellY * RendererVKLayout::LIGHT_GRID_SIZE + cellZ * RendererVKLayout::LIGHT_GRID_SIZE * RendererVKLayout::LIGHT_GRID_SIZE];
								if (cell.numLights < 7)
								{
									cell.lightIds[cell.numLights] = lightIdx;
									cell.numLights++;
								}
							}
						}
					}
				}
			}
		}
	}

	uint16 getHashTableIdx(glm::ivec3 signedVec) {
		glm::uvec3 src = (signedVec << 1) + (signedVec >> 31);
		const uint32 M = 0x5bd1e995u;
		uint32 h = 1190494759u;
		src *= M; src ^= src >> 24u; src *= M;
		h *= M; h ^= src.x; h *= M; h ^= src.y; h *= M; h ^= src.z;
		h ^= h >> 13u; h *= M; h ^= h >> 15u;
		return static_cast<uint16>(h % table.size());
	}
	/*
	uint16 getHashTableIdx(glm::ivec3 pos)
	{
		uint64 hash = (static_cast<uint64>(pos.x) + 6151) << 32 | (static_cast<uint64>(pos.y) + 12289) << 16 | (static_cast<uint64>(pos.z) + 24593);
		hash ^= hash >> 21;
		hash *= 0x9e3779b97f4a7c15ULL; // Golden ratio
		hash ^= hash >> 35;
		return static_cast<uint16>(hash % table.size());
	}*/

	RendererVKLayout::LightGrid* getOrAddGrid(glm::ivec3 pos)
	{
		const uint16 idx = getHashTableIdx(pos);
		RendererVKLayout::LightGridTableEntry& tableEntry = table[idx];
		for (uint16& gridIdx : tableEntry.entries)
		{
			if (gridIdx != UINT16_MAX)
			{
				RendererVKLayout::LightGrid& grid = grids[gridIdx];
				if (grid.gridMin == pos)
					return &grid;
			}
			else
			{
				gridIdx = (uint16)grids.size();
				RendererVKLayout::LightGrid* pGrid = &grids.emplace_back();
				pGrid->gridMin = pos;
				return pGrid;
			}
		}
		assert(false && "Could not add lightgrid due to too many hash collisions! consider changing hash or size");
		return nullptr;
	}

	bool addGridIdx(glm::ivec3 pos, uint16 gridIdx)
	{
		assert(grids.size() < UINT16_MAX - 1);

		const uint16 idx = getHashTableIdx(pos);
		RendererVKLayout::LightGridTableEntry& tableEntry = table[idx];
		for (uint16& entry : tableEntry.entries)
		{
			if (entry != UINT16_MAX)
			{
				entry = gridIdx;
				return true;
			}
		}
		assert(false && "Could not add lightgrid due to too many hash collisions! consider changing hash or size");
		return false;
	}

	uint16 getGridIdx(glm::ivec3 pos)
	{
		const uint16 idx = getHashTableIdx(pos);
		RendererVKLayout::LightGridTableEntry& tableEntry = table[idx];
		for (uint16 gridIdx : tableEntry.entries)
		{
			if (gridIdx == UINT16_MAX)
				continue;
			return gridIdx;
		}
		return UINT16_MAX;
	}
};
