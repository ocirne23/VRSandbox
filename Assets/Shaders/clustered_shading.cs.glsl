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
    float u_fov;
    int u_gridWidth;
    int u_gridHeight;
    int u_gridDepth;
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

layout (binding = 2, std430) writeonly buffer OutLightInfo
{
};

void main()
{
    const uint lightIdx = gl_GlobalInvocationID.x;
    const LightInfo lightInfo = in_lightInfo[lightIdx];
    vec3 boundsOffset = vec3(lightInfo.radius * 1.42);
    vec3 min = lightInfo.pos - boundsOffset;
    vec3 max = lightInfo.pos + boundsOffset
    vec3 projMin = u_mvp * vec4(min, 1.0);
    vec3 projMax = u_mvp * vec4(max, 1.0);
}