#version 460

#extension GL_EXT_ray_query : require
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_nonuniform_qualifier : enable
//#extension GL_EXT_debug_printf : enable

#include "shared.inc.glsl"

#define GI_ALBEDO_LOD 3.0 // sample a coarse mip for hit albedo (no ray-hit derivatives anyway)

struct LightInfo
{
    vec3 pos;
    float range;
    vec3 color;
    float width;
    vec3 direction;
    float rotation;
};
struct MaterialInfo
{
    uint flags;
    float opacity;
    uint diffuseNormalTexIdx;
    uint metalRoughnessTexIdxAlphaMode;
};
struct InMeshInfo
{
    vec3 center;
    float radius;
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint _padding;
};
struct InMeshInstance
{
    uint renderNodeIdx;
    uint instanceOffsetIdx;
    uint meshIdxMaterialIdx;
    uint pipelineIdxAlphaMode;
};

layout (binding = 0, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
    vec4 u_sunDirection;
    vec4 u_sunColor;
    mat4 u_cascadeViewProj[NUM_SHADOW_CASCADES];
    vec4 u_shadowParams;
};
layout (binding = 1, std430) readonly buffer InLightInfos { LightInfo in_lightInfos[]; };
layout (binding = 2, std430) readonly buffer InLightGrid  { uint in_gridData[]; };
layout (binding = 3, std430) readonly buffer InGridTable
{
    uint in_numGrids;
    uint in_gridDataCounter;
    uint in_tableSize;
    uint in_gridTable[];
};
layout (binding = 4) uniform accelerationStructureEXT u_tlas;
layout (binding = 5, std430) readonly buffer InVertices    { float in_vertices[]; }; // MeshVertex as 12 floats
layout (binding = 6, std430) readonly buffer InIndices     { uint in_indices[]; };
layout (binding = 7, std430) readonly buffer InMeshInfos   { InMeshInfo in_meshInfos[]; };
layout (binding = 8, std430) readonly buffer InInstances   { InMeshInstance in_instances[]; };
layout (binding = 9, std430) readonly buffer InMaterials   { MaterialInfo in_materialInfos[]; };
layout (binding = 10) uniform sampler2D u_textures[];
layout (binding = 11) uniform sampler2DArrayShadow u_shadowMap;
layout (binding = 12, std430) coherent buffer ProbeShBuffer { float probe_sh[]; };

layout (push_constant) uniform PushConstants
{
    ivec3 volumeMin; // probe-coordinate of the volume's min corner (world/spacing, snapped to the camera)
    uint  frameIndex;
    uint  numRays;
    float temporalAlpha;
    float maxRayDist;
    float skyIntensity;
} pc;

// Light grid (read) + shared diffuse lighting.
#define GRID_DATA_NAME  in_gridData
#define GRID_TABLE_NAME in_gridTable
#define TABLE_SIZE_NAME in_tableSize
#include "light_grid.inc.glsl"
#include "lighting.inc.glsl"

// GI probe volume (read + write).
#define GI_PROBE_WRITE
#define PROBE_SH_NAME probe_sh
#include "gi_probe.inc.glsl"

uint hashU(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float hashToFloat(uint x) { return float(hashU(x) & 0x00FFFFFFu) / float(0x01000000u); }

vec3 sampleSphere(uint i, uint n, uint seed)
{
    float u1 = fract(float(i) * 0.61803398875 + hashToFloat(seed));
    float u2 = (float(i) + hashToFloat(seed ^ 0x9e3779b9u)) / float(n);
    float z = 1.0 - 2.0 * u1;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u2;
    return vec3(r * cos(phi), r * sin(phi), z);
}

vec3 vNormal(uint vi) { uint b = vi * 12u; return vec3(in_vertices[b + 3u], in_vertices[b + 4u], in_vertices[b + 5u]); }
vec2 vUV(uint vi)     { uint b = vi * 12u; return vec2(in_vertices[b + 10u], in_vertices[b + 11u]); }

vec3 skyRadiance(vec3 dir)
{
    float up = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.55, 0.65, 0.85);
    vec3 zenith  = vec3(0.20, 0.40, 0.85);
    return mix(horizon, zenith, up) * pc.skyIntensity;
}

vec3 traceRadiance(vec3 origin, vec3 dir)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, u_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, origin, 0.05, dir, pc.maxRayDist);
    while (rayQueryProceedEXT(rq)) {}

    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT)
        return skyRadiance(dir);

    const int instanceIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
    const int prim        = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
    const vec2 bc         = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    const float t         = rayQueryGetIntersectionTEXT(rq, true);
    const mat4x3 o2w      = rayQueryGetIntersectionObjectToWorldEXT(rq, true);

    const uint meshIdxMat  = in_instances[instanceIdx].meshIdxMaterialIdx;
    const uint meshIdx     = meshIdxMat & 0x0000FFFFu;
    const uint materialIdx = meshIdxMat >> 16;
    const InMeshInfo mi    = in_meshInfos[meshIdx];

    const uint triBase = mi.firstIndex + uint(prim) * 3u;
    const uint v0 = uint(mi.vertexOffset) + in_indices[triBase + 0u];
    const uint v1 = uint(mi.vertexOffset) + in_indices[triBase + 1u];
    const uint v2 = uint(mi.vertexOffset) + in_indices[triBase + 2u];

    const vec3 b = vec3(1.0 - bc.x - bc.y, bc.x, bc.y);
    const vec3 objN = normalize(b.x * vNormal(v0) + b.y * vNormal(v1) + b.z * vNormal(v2));
    vec3 worldN = normalize(mat3(o2w) * objN);
    const vec2 uv = b.x * vUV(v0) + b.y * vUV(v1) + b.z * vUV(v2);
    const vec3 worldPos = origin + dir * t;
    if (dot(worldN, dir) > 0.0)
        worldN = -worldN;

    const uint diffuseTexIdx = in_materialInfos[materialIdx].diffuseNormalTexIdx & 0x0000FFFFu;
    const vec3 albedo = textureLod(u_textures[nonuniformEXT(diffuseTexIdx)], uv, GI_ALBEDO_LOD).rgb;

    vec3 radiance = giGatherDirect(worldPos, worldN, albedo);
    // Previous-frame indirect at the hit -> multi-bounce (infinite, temporally).
    vec3 prevE = evalProbeSH(worldPos, worldN, pc.volumeMin);
    if (prevE.x >= 0.0)
        radiance += albedo * (prevE / PI);
    return radiance;
}

layout(local_size_x = 64) in;

void main()
{
    const uint id = gl_GlobalInvocationID.x;
    if (id >= uint(GI_PROBE_DIM * GI_PROBE_DIM * GI_PROBE_DIM))
        return;

    const uint z = id / uint(GI_PROBE_DIM * GI_PROBE_DIM);
    const uint rem = id - z * uint(GI_PROBE_DIM * GI_PROBE_DIM);
    const uint y = rem / uint(GI_PROBE_DIM);
    const uint x = rem - y * uint(GI_PROBE_DIM);
    const ivec3 local = ivec3(int(x), int(y), int(z));
    const vec3 probeCenter = vec3(pc.volumeMin + local) * GI_PROBE_SPACING;

    if (id == 0u || id == 100u)
    {
        // DIAGNOSTIC: trace a few axis rays from this probe and report hit distances.
        float tHit[3];
        const vec3 dirs[3] = vec3[3](vec3(0,1,0), vec3(1,0,0), vec3(0,0,-1));
        for (int k = 0; k < 3; ++k)
        {
            rayQueryEXT drq;
            rayQueryInitializeEXT(drq, u_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, probeCenter, 0.001, dirs[k], 1000.0);
            while (rayQueryProceedEXT(drq)) {}
            tHit[k] = (rayQueryGetIntersectionTypeEXT(drq, true) == gl_RayQueryCommittedIntersectionTriangleEXT)
                ? rayQueryGetIntersectionTEXT(drq, true) : -1.0;
        }
        //debugPrintfEXT("[GIDBG] probe %u pos=%v3f  t_up=%f t_x=%f t_-z=%f\n", id, probeCenter, tHit[0], tHit[1], tHit[2]);
    }

    const uint N = max(pc.numRays, 1u);
    const float wsh = 4.0 * PI / float(N);
    const uint seed = hashU(id ^ (pc.frameIndex * 0x9e3779b9u));

    vec3 c0 = vec3(0.0), c1 = vec3(0.0), c2 = vec3(0.0), c3 = vec3(0.0);
    for (uint i = 0u; i < N; ++i)
    {
        const vec3 dir = sampleSphere(i, N, seed);
        const vec3 radiance = traceRadiance(probeCenter, dir);
        const vec4 Y = shBasisL1(dir);
        c0 += radiance * (Y.x * wsh);
        c1 += radiance * (Y.y * wsh);
        c2 += radiance * (Y.z * wsh);
        c3 += radiance * (Y.w * wsh);
    }

    probeBlendSH(id, c0, c1, c2, c3, pc.temporalAlpha);
}
