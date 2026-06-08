#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
//#extension GL_EXT_debug_printf : enable

#include "shared.inc.glsl"

// GI probe allocation + temporal carry pass. One invocation per GRID_SIZE-cube in a camera-centered region
// box. Each invocation: (1) picks the cube's probe cellSize from camera distance, (2) inserts the cube into
// THIS frame's (cur) probe hash grid, (3) appends its grid offset to the trace work list, and (4) carries
// the SH irradiance forward from the PREVIOUS frame's (prev) grid — copying the matching cell, or resampling
// (nearest cell) when the cube's cellSize changed, or zeroing when the cube is new. The cur buffers are
// cleared to EMPTY/0 before this pass; cubes not visited this frame are never re-inserted -> evicted.

layout (binding = 0, std430) coherent buffer CurTable
{
    uint cur_numGrids;
    uint cur_gridCounter;
    uint cur_tableSize;
    uint cur_table[];
};
layout (binding = 1, std430) coherent buffer CurGridData { uint cur_gridData[]; };
layout (binding = 2, std430) coherent buffer CurGridList
{
    uint cur_gridListCount;
    uint cur_gridList[];
};
layout (binding = 3, std430) readonly buffer PrevTable
{
    uint prev_numGrids;
    uint prev_gridCounter;
    uint prev_tableSize;
    uint prev_table[];
};
layout (binding = 4, std430) readonly buffer PrevGridData { uint prev_gridData[]; };

// Bind the include's grid accessors to the cur buffers (write side).
#define GI_PROBE_WRITE
#define GI_GRID_DATA_NAME   cur_gridData
#define GI_TABLE_NAME       cur_table
#define GI_TABLE_SIZE_NAME  cur_tableSize
#define GI_NUM_GRIDS_NAME   cur_numGrids
#define GI_GRID_COUNTER_NAME cur_gridCounter
#include "gi_probe.inc.glsl"

layout (push_constant) uniform PushConstants
{
    ivec3 regionMin; // grid coord of the region's min corner
    uint  regionDim; // cubes per axis
    vec3  viewPos;   // camera position (for cellSize)
} pc;

// --- prev-frame read-only accessors (the include's helpers are bound to the cur buffers) ---
uint prevFindGrid(ivec3 gp)
{
    uint idx = hashTableIndex(gp, prev_tableSize);
    for (uint i = 0u; i < prev_tableSize; ++i)
    {
        uint g = prev_table[idx];
        if (g == EMPTY_ENTRY)
            return EMPTY_ENTRY;
        if (g < INITIALIZING_ENTRY &&
            ivec3(prev_gridData[g + 0u], prev_gridData[g + 1u], prev_gridData[g + 2u]) == gp)
            return g;
        idx = hashNextIndex(idx, prev_tableSize);
    }
    return EMPTY_ENTRY;
}

uint prevCellBase(uint prevGrid, vec3 worldCenter)
{
    ivec3 pgm     = ivec3(prev_gridData[prevGrid + 0u], prev_gridData[prevGrid + 1u], prev_gridData[prevGrid + 2u]);
    uint  cellSize = prev_gridData[prevGrid + 3u];
    int   nc       = int(uint(GI_GRID_SIZE) / cellSize);
    ivec3 cp       = (ivec3(floor(worldCenter)) - pgm * GI_GRID_SIZE) / int(cellSize);
    cp             = clamp(cp, ivec3(0), ivec3(nc - 1));
    uint  cellIdx  = uint(cp.x + cp.y * nc + cp.z * nc * nc);
    return prevGrid + 4u + cellIdx * uint(GI_SH_STRIDE);
}

layout(local_size_x = 64) in;

void main()
{
    const uint id  = gl_GlobalInvocationID.x;
    const uint dim = pc.regionDim;
    if (id >= dim * dim * dim)
        return;

    const uint z   = id / (dim * dim);
    const uint rem = id - z * dim * dim;
    const uint y   = rem / dim;
    const uint x   = rem - y * dim;
    const ivec3 gp = pc.regionMin + ivec3(int(x), int(y), int(z));

    const uint cellSize = giCellSize(gp, pc.viewPos);
    const uint gridIdx  = giGetOrInsertGrid(gp, cellSize);
    if (gridIdx == EMPTY_ENTRY)
        return; // grid-data buffer exhausted

    const uint ord = atomicAdd(cur_gridListCount, 1u);
    cur_gridList[ord] = gridIdx;

    const uint nc    = giNumCellsPerAxis(cellSize);
    const uint cells = nc * nc * nc;
    const uint prevGrid = prevFindGrid(gp);

    for (uint c = 0u; c < cells; ++c)
    {
        const uint cb = giCellBase(gridIdx, c);
        if (prevGrid != EMPTY_ENTRY)
        {
            const uint pcb = prevCellBase(prevGrid, giCellCenter(gridIdx, c));
            for (uint k = 0u; k < uint(GI_SH_STRIDE); ++k)
                cur_gridData[cb + k] = prev_gridData[pcb + k];
        }
        else
        {
            for (uint k = 0u; k < uint(GI_SH_STRIDE); ++k)
                cur_gridData[cb + k] = 0u;
        }
    }
}
