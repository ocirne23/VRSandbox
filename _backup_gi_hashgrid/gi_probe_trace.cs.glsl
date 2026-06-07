#version 460

#extension GL_EXT_ray_query : require
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_nonuniform_qualifier : enable

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
layout (binding = 12, std430) coherent buffer ProbeTableBuffer    { uint probe_table[]; };
layout (binding = 13, std430) coherent buffer ProbeGridDataBuffer { uint probe_gridData[]; };
layout (binding = 14, std430) coherent buffer ProbeShBuffer       { float probe_sh[]; };
layout (binding = 15, std430) coherent buffer ProbeMetaBuffer     { uint probe_numGrids; };

layout (push_constant) uniform PushConstants
{
    ivec3 regionMin;
    uint  regionDim;
    uint  frameIndex;
    uint  numRays;
    float temporalAlpha;
    float maxRayDist;
    float skyIntensity;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

// Light grid (read) + shared diffuse lighting.
#define GRID_DATA_NAME  in_gridData
#define GRID_TABLE_NAME in_gridTable
#define TABLE_SIZE_NAME in_tableSize
#include "light_grid.inc.glsl"
#include "lighting.inc.glsl"

// GI probe grid (read + write).
#define GI_PROBE_WRITE
#define PROBE_TABLE_NAME     probe_table
#define PROBE_GRID_DATA_NAME probe_gridData
#define PROBE_SH_NAME        probe_sh
#define PROBE_NUM_GRIDS_NAME probe_numGrids
#include "gi_probe.inc.glsl"

uint hashU(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float hashToFloat(uint x) { return float(hashU(x) & 0x00FFFFFFu) / float(0x01000000u); }

// Uniform direction on the sphere, decorrelated per probe and rotated per frame.
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

    return vec3(0.5); // DIAGNOSTIC: ray query only, no hit shading (with corrected maxVertex)

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
        worldN = -worldN; // two-sided: face the incoming ray

    const uint diffuseTexIdx = in_materialInfos[materialIdx].diffuseNormalTexIdx & 0x0000FFFFu;
    const vec3 albedo = textureLod(u_textures[nonuniformEXT(diffuseTexIdx)], uv, GI_ALBEDO_LOD).rgb;

    vec3 radiance = giGatherDirect(worldPos, worldN, albedo);
    // Previous-frame indirect at the hit -> multi-bounce (infinite, temporally).
    vec3 prevE = evalProbeSH(worldPos, worldN);
    if (prevE.x >= 0.0)
        radiance += albedo * (prevE / PI);
    return radiance;
}

layout(local_size_x = 64) in;

void main()
{
    const uint id = gl_GlobalInvocationID.x;
    const uint dim = pc.regionDim;
    const uint totalProbes = dim * dim * dim * GI_PROBES_PER_GRID;
    if (id >= totalProbes)
        return;

    const uint regionGridIdx = id / GI_PROBES_PER_GRID;
    const uint probeIdx      = id - regionGridIdx * GI_PROBES_PER_GRID;

    const uint gz = regionGridIdx / (dim * dim);
    const uint grem = regionGridIdx - gz * dim * dim;
    const uint gy = grem / dim;
    const uint gx = grem - gy * dim;
    const ivec3 gp = pc.regionMin + ivec3(int(gx), int(gy), int(gz));

    const uint slot = probeFindSlot(gp);
    if (slot == EMPTY_ENTRY)
        return; // grid not allocated (capacity exhausted)

    const uint lpz = probeIdx / (GI_PROBES_PER_AXIS * GI_PROBES_PER_AXIS);
    const uint lprem = probeIdx - lpz * GI_PROBES_PER_AXIS * GI_PROBES_PER_AXIS;
    const uint lpy = lprem / GI_PROBES_PER_AXIS;
    const uint lpx = lprem - lpy * GI_PROBES_PER_AXIS;
    const vec3 probeCenter = (vec3(gp * GI_PROBES_PER_AXIS) + vec3(float(lpx), float(lpy), float(lpz)) + 0.5) * GI_PROBE_SPACING;

    const uint N = 1u; // DIAGNOSTIC = max(pc.numRays, 1u);
    const float wsh = 4.0 * PI / float(N); // Monte-Carlo SH projection weight (uniform sphere pdf = 1/4pi)
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

    probeBlendSH(slot, probeIdx, c0, c1, c2, c3, pc.temporalAlpha);
}
