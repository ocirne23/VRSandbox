#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_debug_printf : enable

const uint EMPTY_ENTRY        = 0xFFFFFFFFu;
const uint INITIALIZING_ENTRY = 0xEFFFFFFFu;
#define MAX_LARGE_LIGHTS_PER_GRID 6 // Must be even
#define MAX_LIGHTCELL_LIGHTS 12     // Must be even
#define GRID_SIZE 32

struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float intensity;
};
layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};
layout (binding = 1, std430) readonly buffer InLightInfos
{
	LightInfo in_lightInfos[];
};
layout (binding = 2, std430) coherent buffer OutLightGrids
{
    uint in_gridData[];
};
layout (binding = 3, std430) coherent buffer InOutTable
{
    uint inout_numGrids;
    uint inout_gridDataCounter;
    uint inout_tableSize;
    uint inout_table[];
};

/****************************************
struct GRID DATA MEMORY LAYOUT
{
    ivec3 gridMin;
    uint gridSize;
    uint numLargeLights;
    uint16_t largeLightIds[MAX_LARGE_LIGHTS_PER_GRID];
    struct 
    {
        uint numLights;
        uint16_t lightIds[MAX_LIGHTCELL_LIGHTS];
    } cells[gridSize];
};
*/

ivec3 getGridMin(uint gridIdx) 
{ 
    return ivec3(in_gridData[gridIdx + 0], in_gridData[gridIdx + 1], in_gridData[gridIdx + 2]); 
}
void setGridMin(uint gridIdx, ivec3 gridMin)
{
    in_gridData[gridIdx + 0] = gridMin.x;
    in_gridData[gridIdx + 1] = gridMin.y;
    in_gridData[gridIdx + 2] = gridMin.z;
}

uint getCellSize(uint gridIdx) 
{ 
    return in_gridData[gridIdx + 3]; 
}
void setCellSize(uint gridIdx, uint cellSize)
{
    in_gridData[gridIdx + 3] = cellSize;
}
uint getCellOffset(uint gridIdx, uint cellIdx)
{
    return gridIdx + 8 + cellIdx * (MAX_LIGHTCELL_LIGHTS / 2 + 1);
}
uint getNumLightsForCell(uint cellOffset)
{
    return in_gridData[cellOffset];
}
uint16_t getLightId(uint cellOffset, uint lightIdx)
{
    const uint packed = in_gridData[cellOffset + 1 + lightIdx / 2];
    if (lightIdx % 2 == 0)
        return uint16_t(packed & 0x0000FFFF);
    else
        return uint16_t(packed >> 16);
}
void addLightToCell(uint gridIdx, uint cellIdx, uint lightId)
{
    const uint cellOffset = getCellOffset(gridIdx, cellIdx);
    const uint lightIdx = atomicAdd(in_gridData[cellOffset], uint(1));
    if (lightIdx < MAX_LIGHTCELL_LIGHTS)
    {
        atomicOr(in_gridData[cellOffset + 1 + lightIdx / 2], uint(lightId) << ((lightIdx % 2 == 0) ? 0 : 16));
    }
}
void addLargeLight(uint gridIdx, uint lightId)
{
    const uint numLargeLightsOffset = gridIdx + 4;
    const uint largeLightIdx = atomicAdd(in_gridData[numLargeLightsOffset], uint(1));
    if (largeLightIdx < MAX_LARGE_LIGHTS_PER_GRID)
    {
        atomicOr(in_gridData[numLargeLightsOffset + 1 + largeLightIdx / 2], uint(lightId) << ((largeLightIdx % 2 == 0) ? 0 : 16));
    }
}
uint getGridMemoryUsage(uint cellSize)
{
    const uint numCells = GRID_SIZE / cellSize;
    return 4 + (MAX_LARGE_LIGHTS_PER_GRID / 2 + 1) + numCells * numCells * numCells * (MAX_LIGHTCELL_LIGHTS / 2 + 1);
}

uint getPositionHash(ivec3 p) 
{
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return n;
}

uint getOrInsertGrid(ivec3 gridPos, uint cellSize)
{
    const uint gridPosHash = getPositionHash(gridPos);
    uint idx = gridPosHash % inout_tableSize;
    while (true)
    {
        memoryBarrierBuffer();
        uint gridIdx = inout_table[idx];
        if (gridIdx < INITIALIZING_ENTRY && gridIdx < inout_gridDataCounter)
        {
            if (getGridMin(gridIdx) == gridPos)
            {
                return gridIdx;
            }
            idx = (idx + 1) % inout_tableSize;
        }
        else if (atomicCompSwap(inout_table[idx], EMPTY_ENTRY, INITIALIZING_ENTRY) == EMPTY_ENTRY)
        {
            const uint memSize = getGridMemoryUsage(cellSize);
            const uint newGridIdx = atomicAdd(inout_gridDataCounter, memSize);
            setGridMin(newGridIdx, gridPos);
            setCellSize(newGridIdx, cellSize);
            memoryBarrierBuffer();
            atomicExchange(inout_table[idx], newGridIdx);
            atomicAdd(inout_numGrids, uint(1));
            return newGridIdx;
        }
    }
}

ivec3 getGridPos(vec3 pos)
{
    return ivec3(floor(pos / GRID_SIZE));
}

void addLightToGrid(uint gridIdx, uint lightId, vec3 lightMin, vec3 lightMax)
{
    const ivec3 gridMin = getGridMin(gridIdx) * GRID_SIZE;
    const uint cellSize = getCellSize(gridIdx); 

    const uint numCells = GRID_SIZE / cellSize;
    const ivec3 minCell = max(ivec3(floor(lightMin)) - gridMin, ivec3(0)) / ivec3(cellSize);
    const ivec3 maxCell = min(ivec3(floor(lightMax)) - gridMin, ivec3(GRID_SIZE - 1)) / ivec3(cellSize);
    for (uint x = minCell.x; x <= maxCell.x; ++x)
    {
        for (uint y = minCell.y; y <= maxCell.y; ++y)
        {
            for (uint z = minCell.z; z <= maxCell.z; ++z)
            {
                const uint cellIdx = x + y * numCells + z * numCells * numCells;
                addLightToCell(gridIdx, cellIdx, lightId);
            }
        }
    }
}

layout(local_size_x=1, local_size_y=1, local_size_z=1) in;

void main()
{
    const uint lightIdx = gl_GlobalInvocationID.x;
    if (lightIdx < in_lightInfos.length())
    {
        const vec3 lightPos = in_lightInfos[lightIdx].pos;
        const float radius = in_lightInfos[lightIdx].range;
        const vec3 lightMin = lightPos - vec3(radius);
        const vec3 lightMax = lightPos + vec3(radius);
        const ivec3 gridMin = getGridPos(lightMin);
        const ivec3 gridMax = getGridPos(lightMax);

        const uint numGrids = (gridMax.x - gridMin.x + 1) * (gridMax.y - gridMin.y + 1) * (gridMax.z - gridMin.z + 1);
        for (int x = gridMin.x; x <= gridMax.x; ++x)
        {
            for (int y = gridMin.y; y <= gridMax.y; ++y)
            {
                for (int z = gridMin.z; z <= gridMax.z; ++z)
                {
                    const ivec3 gridPos = ivec3(x, y, z);
                    float viewDist = distance(vec3(x, y, z) * GRID_SIZE + GRID_SIZE / 2 , u_viewPos);
                    uint cellSize = 1 << int(mix(0, 8, sqrt(viewDist) / float(GRID_SIZE)));
                    if (cellSize > GRID_SIZE / 2)
                        cellSize = GRID_SIZE;
                    const uint gridIdx = getOrInsertGrid(gridPos, cellSize);
                    if (numGrids <= 8)
                    {
                        addLightToGrid(gridIdx, lightIdx, lightMin, lightMax);
                    }
                    else
                    {
                        addLargeLight(gridIdx, lightIdx);
                    }
                }
            }
        }
    }
}