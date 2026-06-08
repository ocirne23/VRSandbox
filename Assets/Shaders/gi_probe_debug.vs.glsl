#version 450

// GI probe debug visualization: instanced cubes, one per live probe cell. Instance index maps to a
// (grid, cell) via the probe work list; the cube is placed at the cell center and sized with the cell.
// Color = directional irradiance (mode 0) or cellSize/LOD band (mode 1). Invalid instances collapse
// off-screen. Procedural geometry (no vertex buffers): 36 verts = 12 triangles of a unit cube.

#include "shared.inc.glsl"

layout (binding = 0, std140) uniform UBO { mat4 u_mvp; }; // trailing UBO fields ignored here

layout (binding = 1, std430) readonly buffer GiGridData { uint gi_gridData[]; };
layout (binding = 2, std430) readonly buffer GiTable
{
    uint gi_numGrids;
    uint gi_gridCounter;
    uint gi_tableSize;
    uint gi_table[];
};
layout (binding = 3, std430) readonly buffer GiGridList
{
    uint gi_gridListCount;
    uint gi_gridList[];
};

#define GI_GRID_DATA_NAME  gi_gridData
#define GI_TABLE_NAME      gi_table
#define GI_TABLE_SIZE_NAME gi_tableSize
#include "gi_probe.inc.glsl"

layout (push_constant) uniform PC
{
    float u_radius; // sphere/cube radius as a fraction of cell size
    uint  u_mode;   // 0 = irradiance, 1 = cellSize/LOD color
} pc;

layout (location = 0) out vec3 v_color;
layout (location = 1) out vec3 v_normal;

const vec3 CORNERS[8] = vec3[](
    vec3(-0.5, -0.5, -0.5), vec3(0.5, -0.5, -0.5), vec3(0.5, 0.5, -0.5), vec3(-0.5, 0.5, -0.5),
    vec3(-0.5, -0.5,  0.5), vec3(0.5, -0.5,  0.5), vec3(0.5, 0.5,  0.5), vec3(-0.5, 0.5,  0.5));
const int IDX[36] = int[](
    0,1,2, 0,2,3,   4,5,6, 4,6,7,   0,4,5, 0,5,1,
    2,6,7, 2,7,3,   0,3,7, 0,7,4,   1,5,6, 1,6,2);

void main()
{
    uint inst = uint(gl_InstanceIndex);
    uint g    = inst / uint(GI_MAX_CELLS_PER_GRID);
    uint c    = inst - g * uint(GI_MAX_CELLS_PER_GRID);

    bool valid = (g < gi_gridListCount);
    uint gridIdx = 0u, cellSize = 4u;
    if (valid)
    {
        gridIdx  = gi_gridList[g];
        cellSize = giGetCellSize(gridIdx);
        uint nc  = giNumCellsPerAxis(cellSize);
        if (c >= nc * nc * nc)
            valid = false;
    }
    if (!valid)
    {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // outside clip volume -> discarded
        v_color = vec3(0.0);
        v_normal = vec3(0.0, 1.0, 0.0);
        return;
    }

    vec3 corner = CORNERS[IDX[gl_VertexIndex]];
    vec3 center = giCellCenter(gridIdx, c);
    vec3 world  = center + corner * (pc.u_radius * float(cellSize));
    gl_Position = u_mvp * vec4(world, 1.0);

    v_normal = normalize(corner);
    if (pc.u_mode == 1u)
    {
        if      (cellSize <= 4u)  v_color = vec3(1.0, 0.2, 0.2);
        else if (cellSize <= 8u)  v_color = vec3(0.2, 1.0, 0.2);
        else if (cellSize <= 16u) v_color = vec3(0.3, 0.5, 1.0);
        else                      v_color = vec3(1.0, 1.0, 0.2);
    }
    else
    {
        v_color = giEvalCell(giCellBase(gridIdx, c), v_normal) / PI;
    }
}
