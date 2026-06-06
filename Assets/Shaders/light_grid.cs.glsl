#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_debug_printf : enable

#include "shared.inc.glsl"

struct LightInfo
{
	vec3 pos;
	float range;     // negative = tube light 
	vec3 color;
	float width;     // negative = spotlight angle, 0 = point light, > 0 = area/tube light width
	vec3 direction;  // magnitude = height
	float rotation;
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
    uint inout_gridData[];
};
layout (binding = 3, std430) coherent buffer InOutTable
{
    uint inout_numGrids;
    uint inout_gridDataCounter;
    uint inout_tableSize;
    uint inout_gridTable[];
};

#define GRID_DATA_WRITE
#define GRID_DATA_NAME         inout_gridData
#define NUM_GRIDS_NAME         inout_numGrids
#define GRID_DATA_COUNTER_NAME inout_gridDataCounter
#define TABLE_SIZE_NAME        inout_tableSize
#define GRID_TABLE_NAME        inout_gridTable
#include "light_grid.inc.glsl"

layout(local_size_x=1, local_size_y=1, local_size_z=1) in;

void main()
{
    const uint lightIdx   = gl_GlobalInvocationID.x;
    const LightInfo light = in_lightInfos[lightIdx];

    float reach = abs(light.range);
    vec3 lightMin = light.pos - vec3(reach);
    vec3 lightMax = light.pos + vec3(reach);
    if (light.width > 0.0 && light.range < 0.0)
    {
        // Tube light: capsule along the axis. Bound as the two end-cap spheres of radius + absRange.
        const float height   = length(light.direction);
        const float halfLen  = height * 0.5;
        const vec3  axis     = light.direction / height;
        const float absRange = -light.range;
        const float radius   = light.width;
        const float pad      = radius + absRange;
        reach = halfLen + pad; // conservative radius for large-light threshold
        const vec3 pa = light.pos - axis * halfLen;
        const vec3 pb = light.pos + axis * halfLen;
        lightMin = min(pa, pb) - vec3(pad);
        lightMax = max(pa, pb) + vec3(pad);
    }
    else if (light.width > 0.0)
    {
        // Area light: bound the front-facing influence box. The quad only emits along +normal, so
        // the box spans [0, range] on the normal axis (dropping the always-culled back half) and
        // half-extent + range on the in-plane axes. Build the same right/up/normal frame as shading.
        const float height = length(light.direction);
        const vec3 up = light.direction / height;
        const vec3 ref = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        const vec3 right0 = normalize(cross(up, ref));
        const vec3 right = right0 * cos(light.rotation) + cross(up, right0) * sin(light.rotation);
        const vec3 normal = cross(up, right);
        reach += 0.5 * sqrt(light.width * light.width + height * height); // keep the per-cell/large-light threshold conservative
        const float heRight  = light.width * 0.5 + light.range;
        const float heUp     = height * 0.5 + light.range;
        const vec3 boxCenter = light.pos + normal * (light.range * 0.5);
        const vec3 extent = heRight * abs(right) + heUp * abs(up) + (light.range * 0.5) * abs(normal);
        lightMin = boxCenter - extent;
        lightMax = boxCenter + extent;
    }
    else if (light.width < 0.0)
    {
        // Spot light: tighten to the cone's spherical sector (apex + base disk + axial cap tip),
        // which is far smaller than the full range sphere for narrow cones. rotation is the
        // half-angle (<= 90 deg); at 90 deg this collapses to the front-hemisphere box.
        const vec3 axis = normalize(light.direction);
        const vec3 tip  = light.pos + axis * light.range;
        const vec3 c    = light.pos + axis * (light.range * cos(light.rotation));
        const vec3 e    = (light.range * sin(light.rotation)) * sqrt(max(vec3(1.0) - axis * axis, vec3(0.0)));
        lightMin = min(light.pos, min(tip, c - e));
        lightMax = max(light.pos, max(tip, c + e));
    }

    const ivec3 gridMin = getGridPos(lightMin);
    const ivec3 gridMax = getGridPos(lightMax);

    for (int x = gridMin.x; x <= gridMax.x; ++x)
    {
        for (int y = gridMin.y; y <= gridMax.y; ++y)
        {
            for (int z = gridMin.z; z <= gridMax.z; ++z)
            {
                float viewDist = distance(vec3(x, y, z) * GRID_SIZE + GRID_SIZE / 2 , u_viewPos);
                uint cellSize = 1 << int(mix(0, 8, sqrt(viewDist) / float(GRID_SIZE)));
                if (cellSize > GRID_SIZE / 2)
                    cellSize = GRID_SIZE;
                const uint gridIdx = getOrInsertGrid(ivec3(x, y, z), cellSize);
                if (reach <= float(GRID_SIZE / 2))
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