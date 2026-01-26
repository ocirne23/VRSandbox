struct LightInfo
{
	vec3 pos;
	float range;
	vec3 color;
	float intensity;
};
struct LightCell
{
	uint16_t numLights;
	uint16_t lightIds[MAX_LIGHTCELL_LIGHTS];
};
struct LightGrid
{
	ivec3 gridMin;
	float _padding;
	LightCell lightCells[GRID_SIZE * GRID_SIZE * GRID_SIZE];
};
layout (binding = 4, std430) buffer InLightInfos
{
	LightInfo in_lightInfos[];
};
layout (binding = 5, std430) buffer InLightGrid
{
    LightGrid in_lightGrids[];
};
layout (binding = 6, std430) buffer InGridTable
{
    ivec3 in_gridSize;
    uint in_tableSize;
    uint in_gridTable[];
};

void main()
{
    const uint lightIdx = gl_GlobalInvocationID.x;
};