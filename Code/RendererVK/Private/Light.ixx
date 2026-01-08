export module RendererVK:Light;

import Core;
import Core.glm;

import :Layout;

export struct Light : RendererVKLayout::LightInfo
{

};

export template<size_t X, size_t Y, size_t Z>
struct alignas(16) LightGrid
{
	constexpr static size_t NUM_CELLS = X * Y * Z;
	glm::ivec3 m_gridMin;
	float _padding;
	glm::ivec3 m_gridSize = glm::ivec3(X, Y, Z);
	float m_cellSize;

	RendererVKLayout::LightCell m_cells[X * Y * Z];

	uint32 getTotalByteSize() const
	{
		return sizeof(LightGrid<X, Y, Z>);
	}

	uint32 getCellsByteSize() const
	{
		return sizeof(RendererVKLayout::LightCell) * NUM_CELLS;
	}

	uint32 getNumCells() const
	{
		return NUM_CELLS;
	}

	void clear()
	{
		for (int i = 0; i < NUM_CELLS; ++i)
		{
			m_cells[i].numLights = 0;
		}
	}

	void setupGrid(glm::vec3 center, float cellSize)
	{
		m_cellSize = cellSize;
		m_gridMin = glm::ivec3(glm::round(center)) - (glm::ivec3(glm::vec3(m_gridSize) * cellSize) / 2);
		clear();
	}

	void addLight(const Light& light, uint32 lightIdx)
	{
		const glm::ivec3 minCell = glm::ivec3(
			glm::floor((light.pos.x - light.radius - m_gridMin.x) / m_cellSize),
			glm::floor((light.pos.y - light.radius - m_gridMin.y) / m_cellSize),
			glm::floor((light.pos.z - light.radius - m_gridMin.z) / m_cellSize));
		const glm::ivec3 maxCell = glm::ivec3(
			glm::floor((light.pos.x + light.radius - m_gridMin.x) / m_cellSize),
			glm::floor((light.pos.y + light.radius - m_gridMin.y) / m_cellSize),
			glm::floor((light.pos.z + light.radius - m_gridMin.z) / m_cellSize));
		for (int z = minCell.z; z <= maxCell.z; ++z)
		{
			for (int y = minCell.y; y <= maxCell.y; ++y)
			{
				for (int x = minCell.x; x <= maxCell.x; ++x)
				{
					if (x < 0 || x >= m_gridSize.x || y < 0 || y >= m_gridSize.y || z < 0 || z >= m_gridSize.z)
						continue;
					RendererVKLayout::LightCell& cell = m_cells[x + y * m_gridSize.x + z * m_gridSize.x * m_gridSize.y];
					if (cell.numLights < 7)
					{
						cell.lightIds[cell.numLights] = (uint16)lightIdx;
						cell.numLights++;
					}
				}
			}
		}
	}
};