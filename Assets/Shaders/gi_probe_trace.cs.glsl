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
// GI probe clipmap volume (persistent SH, read+write for the multi-bounce lookup + temporal blend).
layout (binding = 12, std430) coherent buffer GiGridData { float gi_gridData[]; };

layout (push_constant) uniform PushConstants
{
    uint  frameIndex;
    uint  numRays;
    float temporalAlpha;
    float maxRayDist;
    vec3  prevViewPos; // last frame's camera (drives the previous clipmap window for freshness)
    float _pad;
} pc;

// Light grid (read) + shared diffuse lighting.
#define GRID_DATA_NAME  in_gridData
#define GRID_TABLE_NAME in_gridTable
#define TABLE_SIZE_NAME in_tableSize
#include "light_grid.inc.glsl"
#include "lighting.inc.glsl"

// GI probe clipmap (read + write).
#define GI_PROBE_WRITE
#define GI_GRID_DATA_NAME    gi_gridData
#include "gi_probe.inc.glsl"

// Alpha-masked-aware shadow rays (sun visibility at gather-ray hits).
#include "rt_shadow.inc.glsl"

uint hashU(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float hashToFloat(uint x) { return float(hashU(x) & 0x00FFFFFFu) / float(0x01000000u); }

// jitter = the two per-probe random offsets (loop-invariant; computed once by the caller).
vec3 sampleSphere(uint i, uint n, vec2 jitter)
{
    float u1 = fract(float(i) * 0.61803398875 + jitter.x);
    float u2 = (float(i) + jitter.y) / float(n);
    float z = 1.0 - 2.0 * u1;
    float r = sqrt(max(0.0, 1.0 - z * z));
    float phi = 2.0 * PI * u2;
    return vec3(r * cos(phi), r * sin(phi), z);
}

vec3 vNormal(uint vi) { uint b = vi * 12u; return vec3(in_vertices[b + 3u], in_vertices[b + 4u], in_vertices[b + 5u]); }
vec2 vUV(uint vi)     { uint b = vi * 12u; return vec2(in_vertices[b + 10u], in_vertices[b + 11u]); }

// View-independent sun visibility from a point: one shadow ray toward the sun via the TLAS. Returns 1
// (lit) or 0 (occluded). Used per gather-ray hit (RT sun mode) so off-screen hits are shadowed
// correctly, unlike the camera-frustum-fit shadow maps.
float sunVisibility(vec3 origin)
{
    return rtShadowVisibility(origin, normalize(u_sunDirection.xyz), 0.05, 1.0e4);
}

vec3 traceRadiance(vec3 origin, vec3 dir, int cascade, out float hitDist)
{
    const float rayMax = pc.maxRayDist * (cascade + 1);
    hitDist = rayMax; // misses (and out-of-bounds hits) count as open space at the gather range
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, u_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, origin, 0.05, dir, rayMax);
    while (rayQueryProceedEXT(rq)) {}

    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT)
        return skyRadiance(dir);

    const int instanceIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
    const int prim        = rayQueryGetIntersectionPrimitiveIndexEXT(rq, true);
    const vec2 bc         = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    const float t         = rayQueryGetIntersectionTEXT(rq, true);
    const mat4x3 o2w      = rayQueryGetIntersectionObjectToWorldEXT(rq, true);

    // Bound every post-hit buffer access. A bad meshIdx/triBase/vertex index would otherwise read wildly
    // out of bounds and MMU-fault; treat any out-of-range hit as a miss.
    if (uint(instanceIdx) >= in_instances.length())
        return skyRadiance(dir);
    const uint meshIdxMat  = in_instances[instanceIdx].meshIdxMaterialIdx;
    const uint meshIdx     = meshIdxMat & 0x0000FFFFu;
    const uint materialIdx = meshIdxMat >> 16;
    if (meshIdx >= in_meshInfos.length() || materialIdx >= in_materialInfos.length())
        return skyRadiance(dir);
    const InMeshInfo mi    = in_meshInfos[meshIdx];

    const uint triBase = mi.firstIndex + uint(prim) * 3u;
    if (triBase + 2u >= in_indices.length())
        return skyRadiance(dir);
    const uint v0 = uint(mi.vertexOffset) + in_indices[triBase + 0u];
    const uint v1 = uint(mi.vertexOffset) + in_indices[triBase + 1u];
    const uint v2 = uint(mi.vertexOffset) + in_indices[triBase + 2u];
    if ((max(max(v0, v1), v2) * 12u + 11u) >= in_vertices.length())
        return skyRadiance(dir);

    const vec3 b = vec3(1.0 - bc.x - bc.y, bc.x, bc.y);
    const vec3 objN = normalize(b.x * vNormal(v0) + b.y * vNormal(v1) + b.z * vNormal(v2));
    vec3 worldN = normalize(mat3(o2w) * objN);
    const vec2 uv = b.x * vUV(v0) + b.y * vUV(v1) + b.z * vUV(v2);
    const vec3 worldPos = origin + dir * t;
    hitDist = t; // committed surface hit -> actual distance to geometry along this ray
    if (dot(worldN, dir) > 0.0)
        worldN = -worldN;

    const uint diffuseTexIdx = in_materialInfos[materialIdx].diffuseNormalTexIdx & 0x0000FFFFu;
    const vec3 albedo = textureLod(u_textures[nonuniformEXT(diffuseTexIdx)], uv, GI_ALBEDO_LOD).rgb;

    // RT sun mode: per-hit sun visibility (one terminate-on-first-hit ray), so shadowed hit points stop
    // contributing sun bounce; evaluating it at the probe center over-brightens GI (probe centers float
    // in open air and their visibility was applied to every hit). Backfacing hits skip the ray: the sun
    // term is NdotL-gated to zero anyway. Cascade mode: override stays unset, hits sample the shadow map.
    if (u_rtSunShadow > 0.5)
        g_sunShadowOverride = dot(worldN, u_sunDirection.xyz) <= 0.0 ? 0.0 : sunVisibility(worldPos + worldN * 0.02);
    vec3 radiance = giGatherDirect(worldPos, worldN, albedo);
    // Previous-frame indirect at the hit -> multi-bounce (infinite, temporally). The cur SH already holds
    // the carried-forward irradiance for this frame.
    vec3 prevE = evalProbeSH(worldPos, worldN);
    if (prevE.x >= 0.0)
        radiance += albedo * (prevE / PI);
    return radiance;
}

layout(local_size_x = 64) in;

void main()
{
    const uint id = gl_GlobalInvocationID.x;
    if (id >= uint(GI_NUM_CASCADES) * uint(GI_CASCADE_PROBES))
        return;

    const int  cascade = int(id / uint(GI_CASCADE_PROBES));
    const uint local   = id - uint(cascade) * uint(GI_CASCADE_PROBES);
    const uint D       = uint(GI_CASCADE_PROBE_DIM);
    const ivec3 oc     = ivec3(int(local % D), int((local / D) % D), int(local / (D * D)));

    const int   spacing    = giCascadeSpacing(cascade);
    const ivec3 lc         = giCascadeOrigin(cascade, u_viewPos) + oc;
    const vec3  probeCenter = vec3(lc) * float(spacing);

    // A probe is "fresh" when its lattice coord was outside the previous frame's clipmap window for this
    // cascade (it just scrolled in), so we replace rather than blend to converge immediately.
    const ivec3 prevOrigin = giCascadeOrigin(cascade, pc.prevViewPos);
    const bool  fresh = any(lessThan(lc, prevOrigin)) || any(greaterThanEqual(lc, prevOrigin + GI_CASCADE_PROBE_DIM));

    const uint N = max(pc.numRays, 1u);
    const float wsh = 4.0 * PI / float(N);
    const uint seed = hashU(id ^ (pc.frameIndex * 0x9e3779b9u));
    const vec2 jitter = vec2(hashToFloat(seed), hashToFloat(seed ^ 0x9e3779b9u));

    vec3 c0 = vec3(0.0), c1 = vec3(0.0), c2 = vec3(0.0), c3 = vec3(0.0);
    float distSum = 0.0;
    for (uint i = 0u; i < N; ++i)
    {
        const vec3 dir = sampleSphere(i, N, jitter);
        float hitDist;
        const vec3 radiance = traceRadiance(probeCenter, dir, cascade, hitDist);
        distSum += hitDist;
        const vec4 Y = shBasisL1(dir);
        c0 += radiance * (Y.x * wsh);
        c1 += radiance * (Y.y * wsh);
        c2 += radiance * (Y.z * wsh);
        c3 += radiance * (Y.w * wsh);
    }

    const uint cellBase = giProbeBase(cascade, lc);
    const float alpha = fresh ? 1.0 : pc.temporalAlpha;
    giBlendCell(cellBase, c0, c1, c2, c3, alpha);
    giBlendMeanDist(cellBase, distSum / float(N), alpha); // mean free-space radius for occlusion at lookup
}
