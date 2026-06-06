// Light grid data structure accessors and utilities
// ReadAPI: Required defines: GRID_DATA_NAME, TABLE_SIZE_NAME, GRID_TABLE_NAME
ivec3 getGridPos(vec3 pos);
ivec3 getGridMin(uint gridIdx);
uint getTableIdx(ivec3 gridPos);
uint getNextTableIdx(uint idx);

uint getGridIdx(uint tableIdx);
uint getCellOffset(uint gridIdx, uint cellIdx);
uint getCellSize(uint gridIdx);
uint getNumLightsForCell(uint cellOffset);
uint getLightId(uint cellOffset, uint lightIdx);

uint getLargeLightCount(uint gridIdx);
uint getLargeLightId(uint gridIdx, uint lightIdx);

#ifdef GRID_DATA_WRITE
// WriteAPI: Required defines: GRID_DATA_WRITE, GRID_DATA_NAME, TABLE_SIZE_NAME, GRID_TABLE_NAME, GRID_DATA_COUNTER_NAME, NUM_GRIDS_NAME
void setGridMin(uint gridIdx, ivec3 gridMin);
void setCellSize(uint gridIdx, uint cellSize);
void addLightToCell(uint gridIdx, uint cellIdx, uint lightId);
void addLargeLight(uint gridIdx, uint lightId);
uint getGridMemoryUsage(uint cellSize);
uint getOrInsertGrid(ivec3 gridPos, uint cellSize);
void addLightToGrid(uint gridIdx, uint lightId, vec3 lightMin, vec3 lightMax);
#endif

#define MAX_LARGE_LIGHTS_PER_GRID 6 // Must be even
#define MAX_LIGHTCELL_LIGHTS 16     // Must be even
#define GRID_SIZE 32

// GRID DATA MEMORY LAYOUT:
// {
//     ivec3 gridMin;
//     uint gridSize;
//     uint numLargeLights;
//     uint16_t largeLightIds[MAX_LARGE_LIGHTS_PER_GRID];
//     struct
//     {
//         uint numLights;
//         uint16_t lightIds[MAX_LIGHTCELL_LIGHTS];
//     } cells[cellSize^3];
// }

#ifndef GRID_DATA_NAME
#error "GRID_DATA_NAME not set"
#endif

#ifndef TABLE_SIZE_NAME
#error "TABLE_SIZE_NAME not set"
#endif

#ifndef GRID_TABLE_NAME
#error "GRID_TABLE_NAME not set"
#endif

uint getPositionHash(ivec3 p) 
{
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
	// pcg hash
	n = n * 747796405u + 2891336453u;
	n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
	n = (n >> 22u) ^ n;
    return n;
}

uint getTableIdx(ivec3 gridPos)
{
    return getPositionHash(gridPos) & (TABLE_SIZE_NAME - 1);
}

uint getGridIdx(uint tableIdx)
{
	return GRID_TABLE_NAME[tableIdx];
}

uint getNextTableIdx(uint idx)
{
	return (idx + 1) & (TABLE_SIZE_NAME - 1);
}

ivec3 getGridPos(vec3 pos)
{
	return ivec3(floor(pos / GRID_SIZE));
}

ivec3 getGridMin(uint gridIdx)
{
	return ivec3(GRID_DATA_NAME[gridIdx + 0], GRID_DATA_NAME[gridIdx + 1], GRID_DATA_NAME[gridIdx + 2]);
}

uint getCellOffset(uint gridIdx, uint cellIdx)
{
	return gridIdx + 8 + cellIdx * (MAX_LIGHTCELL_LIGHTS / 2 + 1);
}

uint getCellSize(uint gridIdx)
{
	return GRID_DATA_NAME[gridIdx + 3];
}

uint getCellIdx(uint gridIdx, ivec3 gridMin, vec3 pos)
{
	const uint cellSize   = getCellSize(gridIdx);
	const uint numCells   = GRID_SIZE / cellSize;
	const ivec3 cellPos   = (ivec3(floor(pos)) - gridMin * GRID_SIZE) / int(cellSize);
	const uint numCellsSq = numCells * numCells;
	const uint cellIdx    = cellPos.x + cellPos.y * numCells + cellPos.z * numCellsSq;
	return cellIdx;
}

uint calcCellOffset(uint gridIdx, ivec3 gridMin, vec3 pos)
{
	const uint cellIdx = getCellIdx(gridIdx, gridMin, pos);
	return getCellOffset(gridIdx, cellIdx);
}

uint getLargeLightCount(uint gridIdx)
{
	return GRID_DATA_NAME[gridIdx + 4];
}

uint getNumLightsForCell(uint cellOffset)
{
	return GRID_DATA_NAME[cellOffset];
}

uint getLightId(uint cellOffset, uint lightIdx)
{
	const uint packed = GRID_DATA_NAME[cellOffset + 1 + lightIdx / 2];
	return (lightIdx & 1u) == 0u ? uint(packed & 0x0000FFFF) : uint(packed >> 16);
}

uint getLargeLightId(uint gridIdx, uint lightIdx)
{
	const uint packed = GRID_DATA_NAME[gridIdx + 5 + lightIdx / 2];
	return (lightIdx & 1u) == 0u ? uint(packed & 0x0000FFFF) : uint(packed >> 16);
}

#ifdef GRID_DATA_WRITE

#ifndef GRID_DATA_COUNTER_NAME
#error "GRID_DATA_COUNTER_NAME not set"
#endif
#ifndef NUM_GRIDS_NAME
#error "NUM_GRIDS_NAME not set"
#endif

void setGridMin(uint gridIdx, ivec3 gridMin)
{
	GRID_DATA_NAME[gridIdx + 0] = gridMin.x;
	GRID_DATA_NAME[gridIdx + 1] = gridMin.y;
	GRID_DATA_NAME[gridIdx + 2] = gridMin.z;
}

void setCellSize(uint gridIdx, uint cellSize)
{
	GRID_DATA_NAME[gridIdx + 3] = cellSize;
}

void addLightToCell(uint gridIdx, uint cellIdx, uint lightId)
{
	const uint cellOffset = getCellOffset(gridIdx, cellIdx);
	const uint lightIdx = atomicAdd(GRID_DATA_NAME[cellOffset], uint(1));
	if (lightIdx < MAX_LIGHTCELL_LIGHTS)
	{
		atomicOr(GRID_DATA_NAME[cellOffset + 1 + lightIdx / 2], uint(lightId) << ((lightIdx & 1u) == 0u ? 0 : 16));
	}
}

void addLargeLight(uint gridIdx, uint lightId)
{
	const uint numLargeLightsOffset = gridIdx + 4;
	const uint largeLightIdx = atomicAdd(GRID_DATA_NAME[numLargeLightsOffset], uint(1));
	if (largeLightIdx < MAX_LARGE_LIGHTS_PER_GRID)
	{
		atomicOr(GRID_DATA_NAME[numLargeLightsOffset + 1 + largeLightIdx / 2], uint(lightId) << ((largeLightIdx & 1u) == 0u ? 0 : 16));
	}
}

uint getGridMemoryUsage(uint cellSize)
{
	const uint numCells = GRID_SIZE / cellSize;
	return 4 + (MAX_LARGE_LIGHTS_PER_GRID / 2 + 1) + numCells * numCells * numCells * (MAX_LIGHTCELL_LIGHTS / 2 + 1);
}

uint getOrInsertGrid(ivec3 gridPos, uint cellSize)
{
	uint idx = getTableIdx(gridPos);
	while (true)
	{
		memoryBarrierBuffer();
		uint gridIdx = GRID_TABLE_NAME[idx];
		if (gridIdx < INITIALIZING_ENTRY && gridIdx < GRID_DATA_COUNTER_NAME)
		{
			if (getGridMin(gridIdx) == gridPos)
			{
				return gridIdx;
			}
			idx = getNextTableIdx(idx);
		}
		else if (atomicCompSwap(GRID_TABLE_NAME[idx], EMPTY_ENTRY, INITIALIZING_ENTRY) == EMPTY_ENTRY)
		{
			const uint memSize = getGridMemoryUsage(cellSize);
			const uint newGridIdx = atomicAdd(GRID_DATA_COUNTER_NAME, memSize);
			setGridMin(newGridIdx, gridPos);
			setCellSize(newGridIdx, cellSize);
			memoryBarrierBuffer();
			atomicExchange(GRID_TABLE_NAME[idx], newGridIdx);
			atomicAdd(NUM_GRIDS_NAME, uint(1));
			return newGridIdx;
		}
	}
}

void addLightToGrid(uint gridIdx, uint lightId, vec3 lightMin, vec3 lightMax)
{
	const ivec3 gridMin = getGridMin(gridIdx) * GRID_SIZE;
	const uint cellSize = getCellSize(gridIdx);

	const uint numCells = GRID_SIZE / cellSize;
	const uint numCellsSq = numCells * numCells;
	const ivec3 minCell = max(ivec3(floor(lightMin)) - gridMin, ivec3(0)) / ivec3(cellSize);
	const ivec3 maxCell = min(ivec3(floor(lightMax)) - gridMin, ivec3(GRID_SIZE - 1)) / ivec3(cellSize);
	for (uint x = minCell.x; x <= maxCell.x; ++x)
	{
		for (uint y = minCell.y; y <= maxCell.y; ++y)
		{
			for (uint z = minCell.z; z <= maxCell.z; ++z)
			{
				const uint cellIdx = x + y * numCells + z * numCellsSq;
				addLightToCell(gridIdx, cellIdx, lightId);
			}
		}
	}
}

#endif