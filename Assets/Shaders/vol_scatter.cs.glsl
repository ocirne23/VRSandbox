#version 460

#extension GL_EXT_ray_query : require

// Volumetric fog scatter: one invocation per froxel. Evaluates the participating-media density at the
// froxel (global height fog with wind-animated noise, plus local fog volume boxes) and the light scattered
// toward the camera through it: the sun (shadowed by a TLAS ray query or a cascade tap), GI probe ambient,
// and the local lights from the world-space light hash grid. The result is temporally blended against last
// frame's reprojected froxel (the Z sample point is jittered per frame, so the blend integrates the slice).
// vol_integrate.cs.glsl then accumulates the grid along Z.

#include "shared.inc.glsl"
#include "vol_fog.inc.glsl"

struct LightInfo
{
    vec3 pos;
    float range;
    vec3 color;
    float width;
    vec3 direction;
    float rotation;
};
struct FogVolume
{
    vec3 pos;
    float density;
    vec3 halfExtents;
    float edgeSoftness;
    vec3 albedo;
    float emissive;
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
layout (binding = 5) uniform sampler2DArrayShadow u_shadowMap;
layout (binding = 6, std430) readonly buffer InFogVolumes
{
    uint in_numFogVolumes;
    uint _fvPad0;
    uint _fvPad1;
    uint _fvPad2;
    FogVolume in_fogVolumes[];
};
layout (binding = 7) uniform sampler3D u_history;
layout (binding = 8, rgba16f) uniform writeonly image3D u_outScatter;
layout (binding = 9, std430) readonly buffer GiGridData { float gi_gridData[]; };

// Light grid (read) + giSquareFalloff/giSunShadow from the shared lighting helpers.
#define GRID_DATA_NAME  in_gridData
#define GRID_TABLE_NAME in_gridTable
#define TABLE_SIZE_NAME in_tableSize
#include "light_grid.inc.glsl"
#include "lighting.inc.glsl"

// GI probe clipmap (read-only): fog picks up bounced/ambient light.
#define GI_GRID_DATA_NAME gi_gridData
#include "gi_probe.inc.glsl"

uint hashU(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
float hashToFloat(uint x) { return float(hashU(x) & 0x00FFFFFFu) / float(0x01000000u); }

float vnLattice(ivec3 p)
{
    const uint h = uint(p.x) * 1597334673u ^ uint(p.y) * 3812015801u ^ uint(p.z) * 2798796415u;
    return hashToFloat(h);
}

float valueNoise(vec3 p)
{
    const ivec3 i = ivec3(floor(p));
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    const float n000 = vnLattice(i + ivec3(0, 0, 0)), n100 = vnLattice(i + ivec3(1, 0, 0));
    const float n010 = vnLattice(i + ivec3(0, 1, 0)), n110 = vnLattice(i + ivec3(1, 1, 0));
    const float n001 = vnLattice(i + ivec3(0, 0, 1)), n101 = vnLattice(i + ivec3(1, 0, 1));
    const float n011 = vnLattice(i + ivec3(0, 1, 1)), n111 = vnLattice(i + ivec3(1, 1, 1));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// Two-octave wind-drifted value noise in [0,1]; modulates the density for a dusty, wispy look.
float fogNoise(vec3 worldPos)
{
    const vec3 wind = vec3(1.0, 0.12, 0.55) * (u_fogParams2.z * u_timeSeconds);
    const vec3 p = (worldPos + wind) * u_fogParams2.x;
    return valueNoise(p) * 0.667 + valueNoise(p * 2.37 + vec3(17.3)) * 0.333;
}

// Hard visibility ray against opaque TLAS geometry (alpha-masked detail is irrelevant at fog frequency).
float volRayVisibility(vec3 origin, vec3 dir, float tMax)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, u_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu, origin, 0.02, dir, tMax);
    while (rayQueryProceedEXT(rq)) {}
    return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionTriangleEXT ? 0.0 : 1.0;
}

// Radiance scattered toward the camera from one grid light: same type encoding (point/spot/rect/tube)
// and falloff as the surface path, but with the HG phase in place of the NdotL/BRDF.
vec3 volLightScatter(LightInfo light, vec3 pos, vec3 viewDir, float g, bool shadowRays)
{
    const vec3 toLight = light.pos - pos;
    const float dist = length(toLight);
    const vec3 L = toLight / max(dist, 1e-4);

    vec3 rad = light.color * giSquareFalloff(dist, abs(light.range));
    if (light.width < 0.0) // spot: rotation = cone half-angle, |direction| = edge softness
    {
        const float softness = length(light.direction);
        const float cosAngle = dot(-L, light.direction / max(softness, 1e-4));
        const float cosOuter = cos(light.rotation);
        const float cosInner = mix(cosOuter, 1.0, softness);
        rad *= smoothstep(cosOuter, cosInner, cosAngle);
    }
    else if (light.width > 0.0 && light.range >= 0.0) // rect area: one-sided along its facing normal
    {
        const float height = length(light.direction);
        const vec3 up     = light.direction / height;
        const vec3 ref    = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
        const vec3 right0 = normalize(cross(up, ref));
        const vec3 right  = right0 * cos(light.rotation) + cross(up, right0) * sin(light.rotation);
        const float facing = dot(cross(up, right), -L);
        if (facing <= 0.0)
            return vec3(0.0);
        rad *= facing;
    }
    if (dot(rad, rad) < 1e-10)
        return vec3(0.0);
    if (shadowRays)
        rad *= volRayVisibility(pos, L, max(dist - 0.05, 0.02));
    return rad * volPhaseHG(dot(viewDir, L), g);
}

layout (local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main()
{
    const ivec3 cell = ivec3(gl_GlobalInvocationID);
    if (any(greaterThanEqual(cell, ivec3(VOL_FROXEL_X, VOL_FROXEL_Y, VOL_FROXEL_Z))))
        return;

    // Per-froxel, per-frame jitter of the Z sample point; the temporal blend integrates it over time.
    const uint seed = hashU(uint(cell.x) ^ hashU(uint(cell.y) * 7919u ^ hashU(uint(cell.z) * 31u ^ (u_frameIndex * 0x9e3779b9u))));
    const float jz = hashToFloat(seed);

    const vec2 vpUv = (vec2(cell.xy) + 0.5) / vec2(VOL_FROXEL_X, VOL_FROXEL_Y);
    const float viewZ = volSliceToViewZ((float(cell.z) + jz) / float(VOL_FROXEL_Z));

    // World position: unproject the viewport-local UV at two depths for the ray, scale by view depth.
    // mvp/invMvp are unjittered (the TAA jitter only applies to rasterization).
    const vec4 clipNdc = vec4(vpUv.x * 2.0 - 1.0, 1.0 - vpUv.y * 2.0, 0.0, 0.0);
    vec4 pN = u_invMvp * vec4(clipNdc.xy, 0.1, 1.0); pN.xyz /= pN.w;
    vec4 pF = u_invMvp * vec4(clipNdc.xy, 0.9, 1.0); pF.xyz /= pF.w;
    const vec3 dir = normalize(pF.xyz - pN.xyz);
    vec4 cN = u_invMvp * vec4(0.0, 0.0, 0.1, 1.0); cN.xyz /= cN.w;
    vec4 cF = u_invMvp * vec4(0.0, 0.0, 0.9, 1.0); cF.xyz /= cF.w;
    const vec3 camFwd = normalize(cF.xyz - cN.xyz);
    const vec3 worldPos = u_viewPos + dir * (viewZ / max(dot(dir, camFwd), 1e-3));

    // ---- Media density + scattering albedo ------------------------------------------------------------
    const float noiseMul = max(1.0 + u_fogParams2.y * (fogNoise(worldPos) * 2.0 - 1.0), 0.0);
    float density = u_fogParams0.x * exp(-max(worldPos.y - u_fogParams0.y, 0.0) * u_fogParams0.z) * noiseMul;
    vec3 albedoWeighted = u_fogParams1.rgb * density;
    vec3 emissive = vec3(0.0);

    for (uint v = 0u; v < in_numFogVolumes; ++v)
    {
        const FogVolume fv = in_fogVolumes[v];
        const vec3 d = abs(worldPos - fv.pos);
        if (any(greaterThan(d, fv.halfExtents)))
            continue;
        // Soft box falloff over the outer edgeSoftness fraction of each half extent.
        const vec3 soft = max(fv.halfExtents * max(fv.edgeSoftness, 1e-3), vec3(1e-3));
        const vec3 t = clamp((fv.halfExtents - d) / soft, vec3(0.0), vec3(1.0));
        const float falloff = t.x * t.y * t.z;
        const float dv = fv.density * falloff * noiseMul;
        density += dv;
        albedoWeighted += fv.albedo * dv;
        emissive += fv.albedo * (fv.emissive * dv);
    }

    vec4 result = vec4(0.0);
    if (density > 1e-6)
    {
        const vec3 albedo = albedoWeighted / density;
        const float g = u_fogParams1.w;

        // Sun, with either TLAS shadow rays or a single cascade tap (matching the surface shadow mode).
        // Multiple rays are jittered in a cone (sun softness) per froxel per frame; together with the
        // temporal blend this turns the binary visibility into a smooth penumbra instead of blotches.
        const vec3 sunDir = normalize(u_sunDirection.xyz);
        float sunVis;
        if (u_rtSunShadow > 0.5)
        {
            const uint numRays = max(uint(u_fogParams4.x), 1u);
            const float softness = u_fogParams4.w;
            const vec3 refAxis = abs(sunDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
            const vec3 t1 = normalize(cross(sunDir, refAxis));
            const vec3 t2 = cross(sunDir, t1);
            sunVis = 0.0;
            for (uint r = 0u; r < numRays; ++r)
            {
                const uint rSeed = hashU(seed ^ (r * 0x68bc21ebu));
                const float ang = hashToFloat(rSeed) * 2.0 * PI;
                const float rad = sqrt(hashToFloat(rSeed ^ 0x9e3779b9u)) * softness;
                const vec3 rayDir = normalize(sunDir + (t1 * cos(ang) + t2 * sin(ang)) * rad);
                sunVis += volRayVisibility(worldPos, rayDir, 1.0e4);
            }
            sunVis /= float(numRays);
        }
        else
            sunVis = giSunShadow(worldPos, vec3(0.0));

        vec3 inLight = atmosTransmittanceToLight(0.0, sunDir, u_skyUp) * u_sunColor.rgb * (volPhaseHG(dot(dir, sunDir), g) * sunVis * u_eclipseParams.x);

        // Ambient: GI probe irradiance toward the camera (toggleable; the clipmap lookup is the next
        // biggest cost after the shadow rays), falling back to the analytic sky. For an SH-L1 field the
        // isotropically in-scattered radiance is E_mean / PI; E(-dir) is the single-sample stand-in for
        // E_mean. The sky fallback covers ~the upper hemisphere, hence the 0.5. u_ambientColor is an
        // isotropic radiance, so its phase integral is just itself.
        vec3 amb = vec3(-1.0);
        if (u_fogParams4.z > 0.5)
            amb = evalProbeSH(worldPos, -dir);
        if (amb.x < 0.0)
            amb = skyRadiance(normalize(u_skyUp)) * (PI);
        inLight += amb / PI + u_ambientColor;

        // Local lights from the world-space hash grid cell containing this froxel.
        const bool lightShadows = u_fogParams3.w > 0.5;
        const ivec3 gridPos = getGridPos(worldPos);
        uint tableIdx = getTableIdx(gridPos);
        while (true)
        {
            const uint gridIdx = getGridIdx(tableIdx);
            if (gridIdx == EMPTY_ENTRY)
                break;
            const ivec3 gridMin = getGridMin(gridIdx);
            if (gridMin == gridPos)
            {
                const uint numLargeLights = getLargeLightCount(gridIdx);
                for (uint i = 0u; i < min(numLargeLights, MAX_LARGE_LIGHTS_PER_GRID); ++i)
                    inLight += volLightScatter(in_lightInfos[getLargeLightId(gridIdx, i)], worldPos, dir, g, lightShadows);

                const uint cellOffset = calcCellOffset(gridIdx, gridMin, worldPos);
                const uint numLights  = getNumLightsForCell(cellOffset);
                for (uint i = 0u; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
                    inLight += volLightScatter(in_lightInfos[getLightId(cellOffset, i)], worldPos, dir, g, lightShadows);
                break;
            }
            tableIdx = getNextTableIdx(tableIdx);
        }

        // rgb = in-scattered radiance per meter, a = extinction (1/m); integrated analytically per slice.
        result = vec4(albedo * density * inLight + emissive, density);
    }

    // ---- Temporal blend against last frame's reprojected froxel ---------------------------------------
    // Reproject from the UNJITTERED froxel center: reprojecting the jittered sample point would smear the
    // per-frame jitter into the history lookup itself (a second noise source on top of the lighting).
    const float centerViewZ = volSliceToViewZ((float(cell.z) + 0.5) / float(VOL_FROXEL_Z));
    const vec3 centerPos = u_viewPos + dir * (centerViewZ / max(dot(dir, camFwd), 1e-3));
    float prevW;
    const vec2 prevFullUv = prevScreenUV(centerPos, prevW);
    const vec2 prevVpUv = (prevFullUv - u_viewportRect.xy) / u_viewportRect.zw;
    if (prevW > VOL_FOG_NEAR
        && all(greaterThanEqual(prevVpUv, vec2(0.0))) && all(lessThanEqual(prevVpUv, vec2(1.0))))
    {
        const float prevSlice = volViewZToSlice(prevW);
        if (prevSlice <= 1.0)
        {
            const vec4 history = texture(u_history, vec3(prevVpUv, prevSlice));
            result = mix(result, history, clamp(u_fogParams2.w, 0.0, 0.97));
        }
    }

    imageStore(u_outScatter, cell, result);
}
