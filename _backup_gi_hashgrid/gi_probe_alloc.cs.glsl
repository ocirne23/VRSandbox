#version 450

#extension GL_EXT_debug_printf : enable

#include "shared.inc.glsl"

// Probe allocation pass: one invocation per 32-unit grid cell in a box region around the camera. Each
// inserts (or finds) its grid in the persistent probe hash table. New grids get a fresh slot whose SH
// block is already zero (the SH buffer is cleared once at startup and never per-frame), so they simply
// converge over the next frames. The table is persistent and never rebuilt, so irradiance survives.

layout (binding = 0, std430) coherent buffer ProbeTableBuffer    { uint probe_table[]; };
layout (binding = 1, std430) coherent buffer ProbeGridDataBuffer { uint probe_gridData[]; };
layout (binding = 2, std430) coherent buffer ProbeMetaBuffer     { uint probe_numGrids; };

#define GI_PROBE_WRITE
#define PROBE_TABLE_NAME     probe_table
#define PROBE_GRID_DATA_NAME probe_gridData
#define PROBE_SH_NAME        probe_sh_unused   // not referenced by the alloc pass
#define PROBE_NUM_GRIDS_NAME probe_numGrids
// evalProbe* (which would reference PROBE_SH_NAME) is unused here; provide a dummy so it compiles.
float probe_sh_unused[1];
#include "gi_probe.inc.glsl"

layout (push_constant) uniform PushConstants
{
    ivec3 regionMin; // grid coord of the region's min corner
    uint  regionDim; // grids per axis (2*radius + 1)
} pc;

layout(local_size_x = 64) in;

void main()
{
    const uint id = gl_GlobalInvocationID.x;
    const uint dim = pc.regionDim;
    if (id >= dim * dim * dim)
        return;

    const uint z = id / (dim * dim);
    const uint rem = id - z * dim * dim;
    const uint y = rem / dim;
    const uint x = rem - y * dim;

    const ivec3 gp = pc.regionMin + ivec3(int(x), int(y), int(z));
    getOrInsertProbeGrid(gp);
}
