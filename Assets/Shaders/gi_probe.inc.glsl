// GI irradiance probes — a separate, persistent, world-space hash grid (sibling of the light grid) that
// stores one SH-L1 RGB irradiance probe per cell. Topology mirrors the light grid exactly: world space is
// partitioned into GRID_SIZE^3 cubes (hash_grid.inc.glsl), each occupied cube owns a dense
// (GRID_SIZE/cellSize)^3 block of cells, and cellSize is chosen per cube from camera distance. The probe
// grid is its own set of buffers (ping-ponged prev/cur across frames) so the light grid stays transient
// and untouched while probe irradiance is carried forward temporally.
//
// Buffers / names the includer must define before including (read side):
//   GI_GRID_DATA_NAME   uint[]  per-grid: [g+0..2]=gridMin, [g+3]=cellSize, then cells[]*GI_SH_STRIDE (SH bits)
//   GI_TABLE_NAME       uint[]  open-addressed hash table: tableIdx -> grid word-offset (EMPTY_ENTRY if free)
//   GI_TABLE_SIZE_NAME  uint    hash table entry count (power of two)
// For the write side (alloc/trace) also define GI_PROBE_WRITE and:
//   GI_NUM_GRIDS_NAME    uint   live grid counter
//   GI_GRID_COUNTER_NAME uint   bump allocator (word offset) into GI_GRID_DATA_NAME
//
// Requires shared.inc.glsl (EMPTY_ENTRY/INITIALIZING_ENTRY, PI) and hash_grid.inc.glsl (GRID_SIZE, hashing,
// getGridPos). GI_* sizing constants must match RendererVKLayout (Layout.ixx).

#ifndef GI_PROBE_INC_GLSL
#define GI_PROBE_INC_GLSL

#include "hash_grid.inc.glsl"

#define GI_SH_STRIDE 12 // SH-L1 RGB: 4 coeffs * 3 channels

// --- Configurable density tuning (override before including to retune) -------------------------------
#ifndef GI_MIN_CELL_LOG2
#define GI_MIN_CELL_LOG2 2 // smallest probe cell = 1<<2 = 4 world units (floor; lights may go finer)
#endif
#ifndef GI_MAX_CELL_LOG2
#define GI_MAX_CELL_LOG2 5 // largest probe cell = 1<<5 = 32 (== GRID_SIZE)
#endif
#ifndef GI_CELL_DIST_SCALE
#define GI_CELL_DIST_SCALE 1.0 // cellLog2 = floor(scale * sqrt(viewDist)); 0.25 matches the light grid
#endif
#ifndef GI_NORMAL_BIAS
#define GI_NORMAL_BIAS 1.0 // push the sample point along the normal (world units) to limit self-leak
#endif
// -----------------------------------------------------------------------------------------------------

#define GI_MAX_CELLS_PER_AXIS (GRID_SIZE >> GI_MIN_CELL_LOG2)
#define GI_MAX_CELLS_PER_GRID (GI_MAX_CELLS_PER_AXIS * GI_MAX_CELLS_PER_AXIS * GI_MAX_CELLS_PER_AXIS)

#ifndef GI_MAX_GRIDS
#define GI_MAX_GRIDS 512u // max live probe cubes (== RendererVKLayout::GI_MAX_GRIDS)
#endif

vec4 shBasisL1(vec3 d)
{
    return vec4(0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x);
}

// Probe cell size (world units, power of two) for the cube at gridPos, given the camera position. Same
// shape as the light grid's formula (sqrt-of-distance buckets) but with an independent floor/ceiling so
// diffuse probes stay coarse enough to be affordable.
uint giCellSize(ivec3 gridPos, vec3 viewPos)
{
    vec3 center = (vec3(gridPos) + 0.5) * float(GRID_SIZE);
    float viewDist = distance(center, viewPos);
    int e = int(GI_CELL_DIST_SCALE * sqrt(viewDist));
    e = clamp(e, GI_MIN_CELL_LOG2, GI_MAX_CELL_LOG2);
    return 1u << uint(e);
}

uint giNumCellsPerAxis(uint cellSize) { return uint(GRID_SIZE) / cellSize; }

uint giGridMemoryUsage(uint cellSize)
{
    uint nc = giNumCellsPerAxis(cellSize);
    return 4u + nc * nc * nc * uint(GI_SH_STRIDE);
}

ivec3 giGetGridMin(uint gridIdx)
{
    return ivec3(GI_GRID_DATA_NAME[gridIdx + 0u], GI_GRID_DATA_NAME[gridIdx + 1u], GI_GRID_DATA_NAME[gridIdx + 2u]);
}

uint giGetCellSize(uint gridIdx) { return GI_GRID_DATA_NAME[gridIdx + 3u]; }

uint giCellBase(uint gridIdx, uint cellIdx) { return gridIdx + 4u + cellIdx * uint(GI_SH_STRIDE); }

// Cell index within a grid for a world position (clamped into the grid).
uint giCellIdx(uint gridIdx, ivec3 gridMin, vec3 pos)
{
    uint cellSize = giGetCellSize(gridIdx);
    int  nc       = int(giNumCellsPerAxis(cellSize));
    ivec3 cp      = (ivec3(floor(pos)) - gridMin * GRID_SIZE) / int(cellSize);
    cp            = clamp(cp, ivec3(0), ivec3(nc - 1));
    return uint(cp.x + cp.y * nc + cp.z * nc * nc);
}

// World-space center of a cell (where the probe lives).
vec3 giCellCenter(uint gridIdx, uint cellIdx)
{
    uint cellSize = giGetCellSize(gridIdx);
    uint nc       = giNumCellsPerAxis(cellSize);
    ivec3 gridMin = giGetGridMin(gridIdx);
    uint z   = cellIdx / (nc * nc);
    uint rem = cellIdx - z * nc * nc;
    uint y   = rem / nc;
    uint x   = rem - y * nc;
    vec3 base = vec3(gridMin) * float(GRID_SIZE);
    return base + (vec3(x, y, z) + 0.5) * float(cellSize);
}

// Read-only open-addressed lookup: world grid coord -> grid word-offset, or EMPTY_ENTRY.
uint giFindGrid(ivec3 gp)
{
    uint idx = hashTableIndex(gp, GI_TABLE_SIZE_NAME);
    for (uint i = 0u; i < GI_TABLE_SIZE_NAME; ++i)
    {
        uint g = GI_TABLE_NAME[idx];
        if (g == EMPTY_ENTRY)
            return EMPTY_ENTRY;
        if (g < INITIALIZING_ENTRY && giGetGridMin(g) == gp)
            return g;
        idx = hashNextIndex(idx, GI_TABLE_SIZE_NAME);
    }
    return EMPTY_ENTRY;
}

// Cosine-convolved irradiance E(n) from one cell's SH-L1. Diffuse exit radiance is albedo/PI * E(n).
vec3 giEvalCell(uint cellBase, vec3 n)
{
    vec3 c0 = vec3(uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 0u]), uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 1u]),  uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 2u]));
    vec3 c1 = vec3(uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 3u]), uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 4u]),  uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 5u]));
    vec3 c2 = vec3(uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 6u]), uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 7u]),  uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 8u]));
    vec3 c3 = vec3(uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 9u]), uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 10u]), uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + 11u]));
    vec4 Y = shBasisL1(n);
    const float A0 = PI;
    const float A1 = 2.0 * PI / 3.0;
    vec3 E = A0 * c0 * Y.x + A1 * (c1 * Y.y + c2 * Y.z + c3 * Y.w);
    return max(E, vec3(0.0));
}

// Trilinear irradiance from the 8 nearest probes. Interpolation spacing is the cell size of the cube
// containing the (normal-biased) sample point; at cellSize/LOD borders neighbours may live in a coarser
// grid, in which case the lookup still resolves to a valid cell (approximate, but borders sit far from the
// camera by construction). Returns vec3(-1) when no probe data exists nearby.
vec3 evalProbeSH(vec3 worldPos, vec3 n)
{
    vec3 p = worldPos + n * GI_NORMAL_BIAS;
    uint gHere = giFindGrid(getGridPos(p));
    if (gHere == EMPTY_ENTRY)
        return vec3(-1.0);

    float cellSize = float(giGetCellSize(gHere));
    vec3 pf = p / cellSize - 0.5;
    ivec3 baseProbe = ivec3(floor(pf));
    vec3 frac = pf - vec3(baseProbe);

    vec3 result = vec3(0.0);
    float totalW = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        vec3 w3 = mix(1.0 - frac, frac, vec3(off));
        float w = w3.x * w3.y * w3.z;
        if (w <= 0.0)
            continue;
        vec3 probeWorld = (vec3(baseProbe + off) + 0.5) * cellSize;

        // Smooth backface weight (DDGI-style): probes lying behind the shaded surface (the surface->probe
        // direction pointing away from the normal) are faded out, which stops irradiance leaking through
        // thin geometry like floors/walls. (dot*0.5+0.5)^2 -> 0 for a probe directly behind, 1 in front.
        vec3 toProbe = probeWorld - worldPos;
        float len = length(toProbe);
        if (len > 1e-4)
        {
            float wn = dot(n, toProbe / len) * 0.5 + 0.5;
            w *= wn * wn;
        }
        if (w <= 0.0)
            continue;

        uint g = giFindGrid(getGridPos(probeWorld));
        if (g == EMPTY_ENTRY)
            continue;
        uint cellIdx = giCellIdx(g, giGetGridMin(g), probeWorld);
        result += w * giEvalCell(giCellBase(g, cellIdx), n);
        totalW += w;
    }
    if (totalW > 1e-4)
        return result / totalW;
    return vec3(-1.0);
}

// Debug visualization: returns a color encoding the probe grid at a surface point. Hue = cellSize / LOD
// band (red=finest 4, green=8, blue=16, yellow=coarsest 32), brightness checkerboarded per probe cell so
// the cell size / probe spacing is directly visible. Black = no probe grid covers this point.
vec3 giDebugColor(vec3 worldPos, vec3 n)
{
    vec3 p = worldPos + n * GI_NORMAL_BIAS;
    uint g = giFindGrid(getGridPos(p));
    if (g == EMPTY_ENTRY)
        return vec3(0.0);

    uint cellSize = giGetCellSize(g);
    vec3 lod;
    if      (cellSize <= 4u)  lod = vec3(1.0, 0.2, 0.2);
    else if (cellSize <= 8u)  lod = vec3(0.2, 1.0, 0.2);
    else if (cellSize <= 16u) lod = vec3(0.3, 0.5, 1.0);
    else                      lod = vec3(1.0, 1.0, 0.2);

    ivec3 gm = giGetGridMin(g);
    ivec3 cp = (ivec3(floor(p)) - gm * GRID_SIZE) / int(cellSize);
    float checker = (((cp.x + cp.y + cp.z) & 1) == 0) ? 1.0 : 0.55;
    return lod * checker;
}

#ifdef GI_PROBE_WRITE

// Insert (or find) the grid at gp, returning its word offset. Open-addressed claim of an EMPTY table
// entry, grid bump-allocated from GI_GRID_COUNTER_NAME. Only the header (gridMin, cellSize) is written
// here; the SH cells are filled by the carry pass. Returns EMPTY_ENTRY if the table/grid-data is full.
//
// DEADLOCK-FREE (unlike the light grid's getOrInsertGrid, which spin-waits on INITIALIZING_ENTRY): this
// pass dispatches many threads per warp (local_size_x=64), so a spin-wait for a sibling invocation to
// finalize a slot would hang the warp (the owner never advances in lockstep). We can avoid waiting
// entirely because every invocation inserts a DISTINCT gridPos (the region cubes are unique) — any
// INITIALIZING/occupied slot we hit therefore belongs to a different key, so we just probe past it
// instead of blocking. A lost CAS likewise re-examines the (now non-EMPTY) slot and advances.
uint giGetOrInsertGrid(ivec3 gp, uint cellSize)
{
    uint idx = hashTableIndex(gp, GI_TABLE_SIZE_NAME);
    for (uint i = 0u; i < GI_TABLE_SIZE_NAME; ++i)
    {
        uint g = GI_TABLE_NAME[idx];
        if (g == INITIALIZING_ENTRY) // owned by another (different) key still initializing -> skip past
        {
            idx = hashNextIndex(idx, GI_TABLE_SIZE_NAME);
            continue;
        }
        if (g != EMPTY_ENTRY) // a finalized grid; ours is unique so it normally won't match, but check
        {
            if (giGetGridMin(g) == gp)
                return g;
            idx = hashNextIndex(idx, GI_TABLE_SIZE_NAME);
            continue;
        }
        // EMPTY: try to claim it.
        if (atomicCompSwap(GI_TABLE_NAME[idx], EMPTY_ENTRY, INITIALIZING_ENTRY) == EMPTY_ENTRY)
        {
            uint mem    = giGridMemoryUsage(cellSize);
            uint newIdx = atomicAdd(GI_GRID_COUNTER_NAME, mem);
            GI_GRID_DATA_NAME[newIdx + 0u] = uint(gp.x);
            GI_GRID_DATA_NAME[newIdx + 1u] = uint(gp.y);
            GI_GRID_DATA_NAME[newIdx + 2u] = uint(gp.z);
            GI_GRID_DATA_NAME[newIdx + 3u] = cellSize;
            memoryBarrierBuffer();
            atomicExchange(GI_TABLE_NAME[idx], newIdx);
            atomicAdd(GI_NUM_GRIDS_NAME, 1u);
            return newIdx;
        }
        // Lost the race for this slot: do not advance; re-read it next iteration (now non-EMPTY -> skip).
    }
    return EMPTY_ENTRY; // table full
}

void giStoreCell(uint cellBase, vec3 c0, vec3 c1, vec3 c2, vec3 c3)
{
    GI_GRID_DATA_NAME[cellBase + 0u]  = floatBitsToUint(c0.r);
    GI_GRID_DATA_NAME[cellBase + 1u]  = floatBitsToUint(c0.g);
    GI_GRID_DATA_NAME[cellBase + 2u]  = floatBitsToUint(c0.b);
    GI_GRID_DATA_NAME[cellBase + 3u]  = floatBitsToUint(c1.r);
    GI_GRID_DATA_NAME[cellBase + 4u]  = floatBitsToUint(c1.g);
    GI_GRID_DATA_NAME[cellBase + 5u]  = floatBitsToUint(c1.b);
    GI_GRID_DATA_NAME[cellBase + 6u]  = floatBitsToUint(c2.r);
    GI_GRID_DATA_NAME[cellBase + 7u]  = floatBitsToUint(c2.g);
    GI_GRID_DATA_NAME[cellBase + 8u]  = floatBitsToUint(c2.b);
    GI_GRID_DATA_NAME[cellBase + 9u]  = floatBitsToUint(c3.r);
    GI_GRID_DATA_NAME[cellBase + 10u] = floatBitsToUint(c3.g);
    GI_GRID_DATA_NAME[cellBase + 11u] = floatBitsToUint(c3.b);
}

// Temporally blend this frame's freshly-projected coefficients into a cell (lerp toward the new value).
void giBlendCell(uint cellBase, vec3 c0, vec3 c1, vec3 c2, vec3 c3, float alpha)
{
    for (uint k = 0u; k < 12u; ++k)
    {
        float prev = uintBitsToFloat(GI_GRID_DATA_NAME[cellBase + k]);
        float next;
        if      (k < 3u)  next = c0[k];
        else if (k < 6u)  next = c1[k - 3u];
        else if (k < 9u)  next = c2[k - 6u];
        else              next = c3[k - 9u];
        GI_GRID_DATA_NAME[cellBase + k] = floatBitsToUint(mix(prev, next, alpha));
    }
}

#endif // GI_PROBE_WRITE

#endif // GI_PROBE_INC_GLSL
