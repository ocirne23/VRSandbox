export module RendererVK:Light;

import Core;
import Core.glm;

import :Layout;

export struct Light : RendererVKLayout::LightInfo
{

};
/*
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

	size_t getNumGrids() const
	{
		return grids.size();
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
		const glm::vec3 lightPosMin = glm::vec3(light.pos - glm::vec3(light.radius));
		const glm::vec3 lightPosMax = glm::vec3(light.pos + glm::vec3(light.radius));

		const glm::ivec3 gridPosMin = glm::ivec3(
			glm::floor(lightPosMin.x / RendererVKLayout::LIGHT_GRID_SIZE),
			glm::floor(lightPosMin.y / RendererVKLayout::LIGHT_GRID_SIZE),
			glm::floor(lightPosMin.z / RendererVKLayout::LIGHT_GRID_SIZE)
		);

		const glm::ivec3 gridPosMax = glm::ivec3(
			glm::floor(lightPosMax.x / RendererVKLayout::LIGHT_GRID_SIZE),
			glm::floor(lightPosMax.y / RendererVKLayout::LIGHT_GRID_SIZE),
			glm::floor(lightPosMax.z / RendererVKLayout::LIGHT_GRID_SIZE)
		);		
		for (int gridX = gridPosMin.x; gridX <= gridPosMax.x; ++gridX)
		{
			for (int gridY = gridPosMin.y; gridY <= gridPosMax.y; ++gridY)
			{
				for (int gridZ = gridPosMin.z; gridZ <= gridPosMax.z; ++gridZ)
				{
					RendererVKLayout::LightGrid* pGrid = getOrAddGrid(glm::ivec3(gridX, gridY, gridZ));
					if (!pGrid)
						continue;

					const glm::ivec3 minCell = glm::max(glm::ivec3(glm::floor(lightPosMin)) - pGrid->gridMin * glm::ivec3(RendererVKLayout::LIGHT_GRID_SIZE), glm::ivec3(0));
					const glm::ivec3 maxCell = glm::min(glm::ivec3(glm::floor(lightPosMax)) - pGrid->gridMin * glm::ivec3(RendererVKLayout::LIGHT_GRID_SIZE), glm::ivec3(RendererVKLayout::LIGHT_GRID_SIZE - 1));
					for (int cellX = minCell.x; cellX <= maxCell.x; ++cellX)
					{
						for (int cellY = minCell.y; cellY <= maxCell.y; ++cellY)
						{
							for (int cellZ = minCell.z; cellZ <= maxCell.z; ++cellZ)
							{
								RendererVKLayout::LightCell& cell = pGrid->cells[cellX + cellY * RendererVKLayout::LIGHT_GRID_SIZE + cellZ * RendererVKLayout::LIGHT_GRID_SIZE * RendererVKLayout::LIGHT_GRID_SIZE];
								if (cell.numLights < RendererVKLayout::MAX_LIGHTS_PER_CELL)
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

	uint16 getHashTableIdx(glm::ivec3 p) {
		uint32 qx = static_cast<uint32>(p.x);
		uint32 qy = static_cast<uint32>(p.y);
		uint32 qz = static_cast<uint32>(p.z);
		qx *= 1597334673u;
		qy *= 3812015801u;
		qz *= 2798796415u;

		uint32 n = qx ^ qy ^ qz;
		n = (n ^ 61u) ^ (n >> 16u);
		n *= 9u;
		n = n ^ (n >> 4u);
		n *= 0x27d4eb2du;
		n = n ^ (n >> 15u);

		return static_cast<uint16>(n % table.size());
	}

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
			else if (grids.size() < RendererVKLayout::MAX_LIGHT_GRIDS)
			{
				gridIdx = (uint16)grids.size();
				RendererVKLayout::LightGrid* pGrid = &grids.emplace_back();
				memset(pGrid->cells->lightIds, 0xFF, sizeof(pGrid->cells->lightIds));
				pGrid->gridMin = pos;
				return pGrid;
			}
		}
		//assert(false && "Could not add lightgrid due to too many hash collisions! consider changing hash or size");
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
*/