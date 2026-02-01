#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_debug_printf : enable

const uint EMPTY_ENTRY        = 0xFFFFFFFFu;
const uint INITIALIZING_ENTRY = 0xEFFFFFFFu;
#define MAX_LIGHTCELL_LIGHTS 6
#define GRID_SIZE 16

struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float intensity;
};
struct LightCell
{
	uint numLights;
	uint16_t lightIds[MAX_LIGHTCELL_LIGHTS];
};
struct LightGrid
{
	ivec3 gridMin;
	uint _padding;
	LightCell lightCells[GRID_SIZE * GRID_SIZE * GRID_SIZE];
};
layout (binding = 1, std430) buffer InLightInfos
{
	LightInfo in_lightInfos[];
};
layout (binding = 2, std430) buffer OutLightGrids
{
    LightGrid in_lightGrids[];
};
layout (binding = 3, std430) buffer InOutTable
{
    uint inout_numGrids;
    uint inout_tableSize;
    uint inout_table[];
};

layout (binding = 4, std430) buffer InInstanceTableBuffer
{
    uint in_instanceTableSize;
    uint in_instanceTable[];
};

uint getHashTableIdx(ivec3 p, uint tableSize) {
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return uint(n % tableSize);
}

bool hasGeometryGrid(ivec3 gridPos)
{
    uint id = getHashTableIdx(gridPos, in_instanceTableSize);
    uint idx = id;
    while (in_instanceTable[idx] != EMPTY_ENTRY)
    {
        if (idx == id)
            return true;
        else
            idx = (idx + 1) % in_instanceTableSize;
    }
    return false;
}

uint getOrInsertGrid(ivec3 gridPos)
{
    uint idx = getHashTableIdx(gridPos, inout_tableSize);
    while (true)
    {
        const uint entry = inout_table[idx];
        if (entry < INITIALIZING_ENTRY)
        {
            if (in_lightGrids[entry].gridMin == gridPos)
            {
                return entry;
            }
            idx = (idx + 1) % inout_tableSize;
        }
        else if (atomicCompSwap(inout_table[idx], EMPTY_ENTRY, INITIALIZING_ENTRY) == EMPTY_ENTRY)
        {
            const uint gridIdx = atomicAdd(inout_numGrids, 1);
            in_lightGrids[gridIdx].gridMin = gridPos;
            inout_table[idx] = gridIdx;
            return gridIdx;
        }
    }
}

ivec3 getGridPos(vec3 pos)
{
    return ivec3(floor(pos / GRID_SIZE));
}

void addLightToGrid(uint gridIdx, uint lightIdx, vec3 lightMin, vec3 lightMax)
{
    const ivec3 gridMin = in_lightGrids[gridIdx].gridMin * GRID_SIZE;
    const ivec3 minCell = max(ivec3(floor(lightMin)) - gridMin, ivec3(0));
    const ivec3 maxCell = min(ivec3(floor(lightMax)) - gridMin, ivec3(GRID_SIZE - 1));
    for (int x = minCell.x; x <= maxCell.x; ++x)
    {
        for (int y = minCell.y; y <= maxCell.y; ++y)
        {
            for (int z = minCell.z; z <= maxCell.z; ++z)
            {
                const uint cellIdx = x + y * GRID_SIZE + z * GRID_SIZE * GRID_SIZE;
                const uint cellLightIdx = atomicAdd(in_lightGrids[gridIdx].lightCells[cellIdx].numLights, uint(1));
                if (cellLightIdx < MAX_LIGHTCELL_LIGHTS)
                {
                    in_lightGrids[gridIdx].lightCells[cellIdx].lightIds[cellLightIdx] = uint16_t(lightIdx);
                }
            }
        }
    }
}

void main()
{
    const uint lightIdx = gl_GlobalInvocationID.x;
    const vec3 lightPos = in_lightInfos[lightIdx].pos;
    const float radius = in_lightInfos[lightIdx].range;
    const vec3 lightMin = lightPos - vec3(radius);
    const vec3 lightMax = lightPos + vec3(radius);
    const ivec3 gridMin = getGridPos(lightMin);
    const ivec3 gridMax = getGridPos(lightMax);

    for (int x = gridMin.x; x <= gridMax.x; ++x)
    {
        for (int y = gridMin.y; y <= gridMax.y; ++y)
        {
            for (int z = gridMin.z; z <= gridMax.z; ++z)
            {
                const ivec3 gridPos = ivec3(x, y, z);
                if (hasGeometryGrid(gridPos))
                {
                    const uint gridIdx = getOrInsertGrid(gridPos);
                    addLightToGrid(gridIdx, lightIdx, lightMin, lightMax);
                }
            }
        }
    }
}