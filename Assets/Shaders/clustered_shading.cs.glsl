#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
};

layout (binding = 1, std140) uniform ClusteredUBO
{
    ivec3 u_gridMin;
    ivec3 u_gridSize;

    float u_fov;
};
struct LightInfo
{
    vec3 pos;
    float range;
    vec3 color;
    float intensity;
};
struct LightBounds
{
    vec3 min;
    vec3 max;
};
layout (binding = 1, std430) readonly buffer InLightInfo
{
    LightInfo in_lightInfo[];
};

struct LightGrid
{
    int count;
    int lightIds[7];
};
layout (binding = 2, std430) writeonly buffer OutLightInfo
{
    LightGrid out_lightGrid;
};

void main()
{
    const uint lightIdx = gl_GlobalInvocationID.x;
    const LightInfo lightInfo = in_lightInfo[lightIdx];
    vec3 boundsOffset = vec3(lightInfo.radius * 1.42);
    ivec3 min = lightInfo.pos - boundsOffset;
    ivec3 max = lightInfo.pos + boundsOffset + ivec3(1,1,1);
    min = 
    for (int x = min.x; x < max.x; ++x)
    {
        for (int y = min.y; y < max.y; ++y)
        {
            for (int z = min.z; z < max.z; ++z)
            {
                
            }
        }
    }
}