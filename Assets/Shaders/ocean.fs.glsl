#version 460

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_query : enable

// FFT ocean surface shading (EPipelineIndex::Ocean). The vertex shader displaced the grid by the simulated
// displacement maps and passed the *undisplaced* world XZ through in_uv; here the gradient maps rebuild a
// per-pixel, mip-filtered wave normal + fold Jacobian, then the surface is shaded from published models:
//   - Fresnel-weighted split between the refracted water body and the reflection (Schlick 1994, F0 = 0.02).
//   - REFRACTION IS RAY-TRACED: the refracted eye ray (Snell, n = 1.33) is traced through the scene TLAS
//     (the water surface itself carries instance mask 0, so rays pass through it); the hit is shaded with
//     its material albedo, sun and GI-probe irradiance, then attenuated by Beer-Lambert absorption
//     exp(-sigma_t * d) along BOTH the underwater view path and the sun's downwelling path to the hit,
//     plus the closed-form single-scatter in-scatter of a homogeneous medium. Misses converge to the
//     infinite-depth in-scatter limit. Grazing pixels (F ~ 1) skip the ray: the body is invisible there.
//   - REFLECTIONS ARE RAY-TRACED too: the mirror ray traces the same TLAS so terrain/objects actually
//     reflect in the water; misses fall back to the analytic sky. Rough/distant water skips the ray
//     (the lobe is too wide for a single mirror sample to represent) and uses the blurred sky instead.
//   - Cook-Torrance sun glint: GGX NDF (Walter et al. 2007; Karis 2013) with height-correlated Smith
//     visibility (Heitz 2014). The microfacet roughness is the tweaked base roughness widened by BOTH
//     geometric specular AA (Kaplanyan et al. 2016 / Filament) AND the LEAN-filtered slope variance
//     (Olano & Baker 2010; Bruneton, Neyret & Holzschuch 2010, "Real-time Realistic Ocean Lighting"):
//     wave slopes the gradient mips filtered out at distance return as roughness, which produces the
//     correct elongated glittering sun path to the horizon. One RT shadow ray gates the sun.
//   - Whitecap foam from Jacobian folding (Tessendorf 2001) with temporal accumulation
//     (ocean_foam.cs.glsl): injected where crests fold, then lingers and decays like real foam.
// Sky reflection/ambient reuse the engine's atmosphere model (skyRadiance), so the water tracks the live
// sky/sun tweaks.

#include "shared.inc.glsl"
#define OCEAN_SHORE_BINDING 18
#define TERRAIN_HEIGHT_BINDING 19
#include "ocean_wave.inc.glsl"

struct MaterialInfo
{
    uint flags;
    float opacity;
    uint diffuseNormalTexIdx;
    uint metalRoughnessTexIdxAlphaMode;
};
layout (binding = 2, std430) readonly buffer InMaterialInfos { MaterialInfo in_materialInfos[]; };

// Scene lights: the same world-space light grid the forward pass shades with (bindings shared through
// the static-mesh pipeline layout).
struct LightInfo
{
    vec3 pos;
    float range;
    vec3 color;
    float width;     // 0 = point light, > 0 = rectangular area light, < 0 = spot
    vec3 direction;  // area light: up-axis, magnitude = height
    float rotation;  // area light: rotation of the quad around direction
};
layout (binding = 4, std430) readonly buffer InLightInfos { LightInfo in_lightInfos[]; };
layout (binding = 5, std430) readonly buffer InLightGrid { uint in_gridData[]; };
layout (binding = 6, std430) readonly buffer InGridTable
{
    uint in_numGrids;
    uint in_gridDataCounter;
    uint in_tableSize;
    uint in_gridTable[];
};
#define GRID_DATA_NAME  in_gridData
#define GRID_TABLE_NAME in_gridTable
#define TABLE_SIZE_NAME in_tableSize
#include "light_grid.inc.glsl"

layout (binding = 20) uniform sampler2D u_textures[]; // highest binding in the set: variable descriptor count
layout (binding = 11) uniform accelerationStructureEXT u_tlas;

// Scene geometry for ray hits (same buffers the TLAS instance records were built from — custom index =
// index into in_instances; RT meshIdx rides the instance sbtOffset).
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
layout (binding = 14, std430) readonly buffer InRTVertices  { float in_vertices[]; }; // MeshVertex as 12 floats
layout (binding = 15, std430) readonly buffer InRTIndices   { uint in_indices[]; };
layout (binding = 16, std430) readonly buffer InRTMeshInfos { InMeshInfo in_meshInfos[]; };
layout (binding = 17, std430) readonly buffer InRTInstances { InMeshInstance in_instances[]; };

// GI irradiance probes: light ray hit points with the same indirect the scene gets.
layout (binding = 10, std430) readonly buffer GiGridData { float gi_gridData[]; };
#define GI_GRID_DATA_NAME gi_gridData
#include "gi_probe.inc.glsl"

#include "rt_shadow.inc.glsl"       // rtShadowVisibility + the alpha-masked candidate test
#include "punctual_lights.inc.glsl" // point/spot/area/tube evaluation + RT light shadows (shared with the forward pass)

layout (location = 0) in vec3 in_pos;                       // displaced world position
layout (location = 1) in mat3 in_tbn;                       // placeholder (normal rebuilt here)
layout (location = 4) in vec2 in_uv;                        // undisplaced world XZ
layout (location = 5) in flat uint in_meshIdxMaterialIdx;
#ifdef STEREO
layout (push_constant) uniform ViewPC { uint u_viewIndex; }; // selects the per-eye view (1=left, 2=right) in VR
#endif

layout (location = 0) out vec4 out_color;

// --- Cook-Torrance pieces --------------------------------------------------------------------------
float D_GGX(float NoH, float a) // Trowbridge-Reitz (Walter et al. 2007), a = alpha = roughness^2
{
    float a2 = a * a;
    float d  = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * d * d);
}
float V_SmithGGX(float NoV, float NoL, float a) // height-correlated Smith (Heitz 2014), includes 1/(4 NoV NoL)
{
    float a2 = a * a;
    float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}
float F_Schlick(float u, float f0)
{
    float x = clamp(1.0 - u, 0.0, 1.0);
    float x2 = x * x;
    return f0 + (1.0 - f0) * (x2 * x2 * x);
}
// Geometric specular AA (Kaplanyan et al. 2016 / Filament): sub-pixel normal variance from screen-space
// derivatives, added to alpha^2 so the lobe covers it instead of flickering across it.
float normalVariance(vec3 N)
{
    vec3 dNdx = dFdx(N);
    vec3 dNdy = dFdy(N);
    return min(0.5 * (dot(dNdx, dNdx) + dot(dNdy, dNdy)), 0.18);
}

// --- Reflected sky ----------------------------------------------------------------------------------
// Mirror of sky.fs.glsl's atmosphere core — same step counts (12 sun / 4 sky-radiance vs skyRadiance()'s
// GI-grade 4 / 2) and the same saturation + eclipse treatment — so the sky reflected in the water matches
// the sky actually rendered above it. The GI-grade integrator under-samples the long grazing paths water
// reflections look along (2 midpoint samples land far past the near region where the in-scatter
// accumulates), which is why u_skyRadianceColor barely registered in reflections. Clouds/stars/moon are
// not re-marched here (they'd need a sky capture); the sun disc is intentionally absent — the GGX glint
// is its reflection.
vec3 adjustSaturation(vec3 color, float saturation)
{
    const vec3 luminosity = vec3(0.2126, 0.7152, 0.0722);
    return mix(vec3(dot(color, luminosity)), color, saturation);
}
vec3 reflectedSkyRadiance(vec3 dir)
{
    const vec3 up = normalize(u_skyUp);
    const float eclipse = u_eclipseParams.x;
    vec3 color = atmosphereScatterCheap(dir, normalize(u_sunDirection.xyz), up, 12) * u_sunColor.rgb;
    color = adjustSaturation(color, 2.0 * (2.0 - eclipse)) * eclipse; // sky.fs's eclipse saturation push
    if (dot(u_skyRadianceColor, u_skyRadianceColor) > 0.0)
        color += atmosphereScatterCheap(dir, up, up, 4) * u_skyRadianceColor;
    return color;
}

// --- Shared closest-hit trace + shading -------------------------------------------------------------
struct SceneHit
{
    float t;
    vec3 pos;
    vec3 N;
    vec3 albedo;
};

bool traceScene(vec3 origin, vec3 dir, float tMax, out SceneHit hit)
{
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, u_tlas, gl_RayFlagsNoneEXT, 0xFFu, origin, 0.05, dir, tMax);
    while (rayQueryProceedEXT(rq))
    {
        if (rayQueryGetIntersectionTypeEXT(rq, false) == gl_RayQueryCandidateIntersectionTriangleEXT
            && rtsCandidateBlocks(rq))
            rayQueryConfirmIntersectionEXT(rq);
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionTriangleEXT)
        return false;

    hit.t = rayQueryGetIntersectionTEXT(rq, true);
    hit.pos = origin + dir * hit.t;
    hit.N = -dir;
    hit.albedo = vec3(0.3);

    // Interpolated normal/uv + material albedo, bounds-checked like rt_shadow.inc.glsl.
    const int instanceIdx = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
    const uint meshIdx = rayQueryGetIntersectionInstanceShaderBindingTableRecordOffsetEXT(rq, true);
    if (uint(instanceIdx) < in_instances.length() && meshIdx < in_meshInfos.length())
    {
        const InMeshInfo mi = in_meshInfos[meshIdx];
        const uint triBase = mi.firstIndex + uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true)) * 3u;
        if (triBase + 2u < in_indices.length())
        {
            const uint v0 = uint(mi.vertexOffset) + in_indices[triBase + 0u];
            const uint v1 = uint(mi.vertexOffset) + in_indices[triBase + 1u];
            const uint v2 = uint(mi.vertexOffset) + in_indices[triBase + 2u];
            if ((max(max(v0, v1), v2) * 12u + 11u) < in_vertices.length())
            {
                const vec2 bc = rayQueryGetIntersectionBarycentricsEXT(rq, true);
                const vec3 w = vec3(1.0 - bc.x - bc.y, bc.x, bc.y);
                #define RT_V(vi, o) vec3(in_vertices[(vi) * 12u + (o)], in_vertices[(vi) * 12u + (o) + 1u], in_vertices[(vi) * 12u + (o) + 2u])
                const vec3 objN = RT_V(v0, 3u) * w.x + RT_V(v1, 3u) * w.y + RT_V(v2, 3u) * w.z;
                const vec2 uv = w.x * rtsVertexUV(v0) + w.y * rtsVertexUV(v1) + w.z * rtsVertexUV(v2);
                hit.N = normalize(mat3(rayQueryGetIntersectionObjectToWorldEXT(rq, true)) * objN);
                if (dot(hit.N, dir) > 0.0)
                    hit.N = -hit.N;

                const uint materialIdx = in_instances[instanceIdx].meshIdxMaterialIdx >> 16;
                if (materialIdx < in_materialInfos.length())
                {
                    const uint diffuseTexIdx = in_materialInfos[materialIdx].diffuseNormalTexIdx & 0x0000FFFFu;
                    const float lod = clamp(log2(max(hit.t, 1.0)) + 1.0, 0.0, 7.0); // ray-cone-ish LOD by distance
                    hit.albedo = textureLod(u_textures[nonuniformEXT(diffuseTexIdx)], uv, lod).rgb;
                }
            }
        }
    }
    return true;
}

// Shading of a ray hit: sun (optionally attenuated) + GI probe irradiance (skyRadiance fallback), and —
// behind the OCEAN_HIT_LIGHTS variant define ("Ocean/Shading/Hit lighting" tweak) — the scene's grid
// lights, so geometry seen THROUGH the water (refraction) or mirrored in it (reflection) picks up local
// light. Analytic only: no per-light shadow rays inside what is already a refraction/reflection ray.
vec3 shadeHit(SceneHit hit, vec3 rayDir, vec3 sunRadiance, vec3 L)
{
    const vec3 sun = sunRadiance * max(dot(hit.N, L), 0.0);
    float giCoverage;
    const vec3 probeE = evalProbeSHCoverage(hit.pos, hit.N, giCoverage);
    vec3 indirect = probeE.x >= 0.0 ? probeE / PI : vec3(0.0);
    if (giCoverage < 1.0) // fade to the sky+ground fallback over the probe field's outer band (no boundary step)
        indirect = mix(skyGroundRadiance(hit.N), indirect, giCoverage);
    indirect *= u_aoParams.y;
    // Underwater hits: the ambient/GI reaching the bottom enters at the surface and Beer-Lamberts down
    // the water column, like the sun path the caller already attenuates. Neither the probes nor the sky
    // fallback know about the water (the ocean surface is excluded from the TLAS), so without this the
    // seabed reads open-air-lit at any depth — and since the two sides disagree on how bright that is,
    // the probe-field boundary showed on shallow water. Above-water hits (reflections) have zero depth.
    const vec3 ambientAtten = exp(-u_oceanAbsorption.rgb * max(u_oceanParams2.w - hit.pos.y, 0.0));
    vec3 radiance = hit.albedo * (sun / PI + (indirect + u_ambientColor) * ambientAtten);

#ifdef OCEAN_HIT_LIGHTS
    const vec3 matColOverPi = hit.albedo / PI;
    const vec3 V = -rayDir;
    const ivec3 gridPos = getGridPos(hit.pos);
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
            for (uint i = 0; i < min(numLargeLights, MAX_LARGE_LIGHTS_PER_GRID); ++i)
                radiance += doLight(in_lightInfos[getLargeLightId(gridIdx, i)], hit.pos, V, hit.N, vec3(0.04), matColOverPi, 0.0, 0.7, 0.49);
            const uint cellOffset = calcCellOffset(gridIdx, gridMin, hit.pos);
            const uint numLights = getNumLightsForCell(cellOffset);
            for (uint i = 0; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
                radiance += doLight(in_lightInfos[getLightId(cellOffset, i)], hit.pos, V, hit.N, vec3(0.04), matColOverPi, 0.0, 0.7, 0.49);
            break;
        }
        tableIdx = getNextTableIdx(tableIdx);
    }
#endif
    return radiance;
}

void main()
{
#ifdef STEREO
    g_viewIndex = int(u_viewIndex);
#endif
    const vec3 up = normalize(u_skyUp);
    const vec3 L  = normalize(u_sunDirection.xyz);
    const vec3 V  = normalize(u_viewPos - in_pos);

    // Per-pixel wave normal, fold Jacobian, LEAN slope variance and vertical acceleration from the
    // simulated maps (implicit LOD: the mip chain prefilters, the variance recovers what it removed).
    vec2 slope, slopeVar;
    float jacobian, accel;
    oceanSampleSurface(in_uv, slope, jacobian, slopeVar, accel);

    // Accumulated TURBULENCE — the decaying churn energy of past breaking (derivatives need uniform
    // control flow, so sampled unconditionally here). It drives three effects below: aged foam via
    // fold-threshold relaxation (the pattern comes from the LIVE geometry's convergence lines, so the
    // wake reads as churned water, not a decal), entrained-bubble milkiness, and extra roughness.
    const float turbulence = oceanSampleTurbulence(in_uv, length(fwidth(in_uv)));
    const float foam = oceanInstantFoam(jacobian, accel, turbulence * u_oceanParams5.x);

    // Shoreline surf (needs a shore depth map + "Shore foam depth" > 0): where the swell runs into
    // vanishing depth the crest occupies most of the remaining water column and churns white (Miche-ish
    // breaking, H ~ 0.8 d), plus a thin lace right at the waterline. Driven by the LIVE displaced surface
    // height, so the band surges up the beach and drains back with each wave instead of sitting on a
    // static depth contour.
    float shoreFoam = 0.0;
    const float shoreFoamDepth = u_oceanParams5.z;
    if (shoreFoamDepth > 0.0)
    {
        const float shoreDepth = oceanSampleShoreDepth(in_uv);
        const float waveH = in_pos.y - u_oceanParams2.w;        // surface height above the calm sea level
        const float column = max(shoreDepth, 0.0) + waveH;      // instantaneous water column at this pixel
        const float nearShore = 1.0 - smoothstep(shoreFoamDepth, 4.0 * shoreFoamDepth, shoreDepth);
        const float bore = max(waveH, 0.0) / max(column, 0.05); // crest fraction of the column (bore front)
        shoreFoam = nearShore * smoothstep(0.4, 0.8, bore);
        shoreFoam = max(shoreFoam, 1.0 - smoothstep(0.0, 0.35 * shoreFoamDepth, column)); // waterline lace
    }
    const float ns = u_oceanParams1.w;
    vec3 N = normalize(vec3(-slope.x * ns, 1.0, -slope.y * ns));
    if (dot(N, V) < 0.0) // grazing/underwater: keep the shading hemisphere consistent
        N = -N;

    const float NoV = clamp(dot(N, V), 1e-3, 1.0);
    const float NoL = max(dot(N, L), 0.0);
    const vec3  H   = normalize(L + V);
    const float NoH = max(dot(N, H), 0.0);
    const float LoH = max(dot(L, H), 0.0);

    // Microfacet roughness = base + geometric specular AA + LEAN filtered slope variance (slope variance
    // maps to alpha^2 ~ 2 sigma^2; Bruneton 2010). This is what stretches the sun glitter toward the
    // horizon instead of leaving distant water a flat mirror.
    const float perceptualRough = clamp(u_oceanAbsorption.w, 0.02, 1.0);
    const float slopeVariance = 0.5 * (slopeVar.x + slopeVar.y) * (ns * ns);
    // Turbulent water is micro-rough (churned surface + bubbles): scatter the glint in the wake.
    const float alphaSq = perceptualRough * perceptualRough * perceptualRough * perceptualRough
        + normalVariance(N) + 2.0 * slopeVariance + turbulence * u_oceanParams5.y * 0.35;
    const float alphaF = clamp(sqrt(alphaSq), 0.02, 1.0);

    // Sun radiance at the surface (atmosphere-attenuated + eclipse-scaled) + one RT shadow ray so
    // terrain and objects actually shade the water.
    const vec3 sunTint = u_sunColor.rgb * atmosTransmittanceToLight(0.0, L, up) * u_eclipseParams.x;
    const float sunVis = NoL > 0.0 ? rtShadowVisibility(in_pos + N * 0.1, L, 0.05, 10000.0) : 0.0;
    const vec3 ambientSky = skyRadiance(up);
    // Diffusely lit whitewater: used for the crest foam and (dimmer) the entrained-bubble milkiness.
    const vec3 whitewater = u_oceanFoam.rgb * (sunTint * (NoL * sunVis) / PI + ambientSky + u_ambientColor);

    // In-scattered radiance of the water body (the color of deep water).
    const vec3 sigmaT = u_oceanAbsorption.rgb;
    const vec3 inscatter = u_oceanScatter.rgb * u_oceanScatter.w * (ambientSky + sunTint * max(L.y, 0.0) / PI);

    // Fresnel split early: it gates which rays are worth tracing.
    const float F = F_Schlick(NoV, 0.02);

    // --- Refracted body: Snell into the water, ray-traced, Beer-Lambert absorbed both ways ---
    vec3 body = inscatter;
    if (F < 0.98) // at grazing the transmitted term is invisible: skip the ray
    {
        const vec3 refrDir = refract(-V, N, 1.0 / 1.33);
        if (dot(refrDir, refrDir) > 1e-6)
        {
            const float minSigma = max(min(sigmaT.r, min(sigmaT.g, sigmaT.b)), 1e-3);
            SceneHit hit;
            bool haveHit = traceScene(in_pos, refrDir, min(4.6 / minSigma, 400.0), hit); // bounded at ~99% extinction
            if (!haveHit && refrDir.y < -0.02)
            {
                // The ray found no geometry — beyond RT/GI/TLAS Range the seabed simply isn't in the TLAS,
                // which cut the Beer-Lambert body color off at that distance. Approximate the bottom from
                // the baked terrain height field instead (shore map + fog cascade fallback, valid for many
                // km): intersect the refracted ray with the water column analytically and shade a flat
                // ground-albedo pseudo-hit; the absorption/inscatter composition below is identical.
                float depth = oceanSampleShoreDepth(in_pos.xz);
                float t = depth / -refrDir.y;
                depth = oceanSampleShoreDepth(in_pos.xz + refrDir.xz * t); // one refinement for sloped shelves
                if (depth > 0.0)
                {
                    hit.t = clamp(depth / -refrDir.y, 0.0, 4.6 / minSigma);
                    hit.pos = in_pos + refrDir * hit.t;
                    hit.N = vec3(0.0, 1.0, 0.0);
                    hit.albedo = vec3(1.0);
                    haveHit = true;
                }
            }
            if (haveHit)
            {
                // Sun reaching the hit attenuates down the water column above it (Beer-Lambert), then the
                // view path back attenuates again; plus closed-form homogeneous single-scatter in-scatter.
                const float sunPath = max(u_oceanParams2.w - hit.pos.y, 0.0) / max(L.y, 0.25);
                const vec3 hitRadiance = shadeHit(hit, refrDir, sunTint * sunVis * exp(-sigmaT * sunPath), L);
                const vec3 T = exp(-sigmaT * hit.t);
                body = hitRadiance * T + inscatter * (1.0 - T);
            }
        }
    }

    // --- Reflection: ray-traced mirror against the scene; sky fallback; roughness-blurred toward the
    // average sky (a wide lobe can't be represented by one mirror sample, so rough water skips the ray) ---
    vec3 R = reflect(-V, N);
    R.y = max(R.y, 0.02); // keep grazing reflections just above the horizon
    R = normalize(R);
    const float reflBlur = clamp(alphaF * 2.0 - 0.05, 0.0, 0.6);
    vec3 reflColor = reflectedSkyRadiance(R); // sky-matched atmosphere (skyRadiance() is GI-grade: too few steps at grazing)
    if (alphaF < 0.25)
    {
        SceneHit hit;
        if (traceScene(in_pos + N * 0.05, R, 3000.0, hit))
            reflColor = shadeHit(hit, R, sunTint, L);
    }
    const vec3 reflection = mix(reflColor, ambientSky, reflBlur);

    // Entrained bubbles: turbulent water turns milky — churned wakes stay visibly brighter/whiter even
    // after the surface foam has thinned to streaks ("Ocean/Foam/Turbidity").
    body = mix(body, whitewater * 0.55, clamp(turbulence * u_oceanParams5.y, 0.0, 1.0));

    vec3 color = mix(body, reflection, F);

    // --- Sun glint: full Cook-Torrance specular, shadow-gated ---
    const float D  = D_GGX(NoH, alphaF);
    const float Vv = V_SmithGGX(NoV, NoL, alphaF);
    color += sunTint * (D * Vv * NoL * sunVis) * F_Schlick(LoH, 0.02);

    // --- Crest foam (geometry-locked instant foam, threshold-relaxed by the accumulated turbulence)
    // plus the shoreline surf band ---
    const float foamW = clamp(max(foam, shoreFoam), 0.0, 1.0);
    if (foamW > 0.003)
        color = mix(color, whitewater, foamW);

    // --- Scene lights: the shared light-grid walk + punctual/area/tube evaluation (RT-shadowed), applied
    // to the final surface: water responds with the dielectric F0 = 0.02 specular (the light's glint,
    // widened by the same filtered roughness as the sun) and an in-scatter "diffuse" (light entering the
    // water and scattering back out); foam patches respond as bright lambertian whitewater instead.
    {
        const vec3 matColOverPi = mix(u_oceanScatter.rgb * u_oceanScatter.w, u_oceanFoam.rgb, foamW) / PI;
        const float lightRough = clamp(mix(alphaF, 0.85, foamW), 0.02, 1.0);
        const float lightRoughSq = lightRough * lightRough;
        const vec3 waterSpec = vec3(0.02);

        const ivec3 gridPos = getGridPos(in_pos);
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
                for (uint i = 0; i < min(numLargeLights, MAX_LARGE_LIGHTS_PER_GRID); ++i)
                {
                    const LightInfo light = in_lightInfos[getLargeLightId(gridIdx, i)];
                    color += doLightShadowed(light, in_pos, V, N, waterSpec, matColOverPi, 0.0, lightRough, lightRoughSq);
                }
                const uint cellOffset = calcCellOffset(gridIdx, gridMin, in_pos);
                const uint numLights = getNumLightsForCell(cellOffset);
                for (uint i = 0; i < min(numLights, MAX_LIGHTCELL_LIGHTS); ++i)
                {
                    const LightInfo light = in_lightInfos[getLightId(cellOffset, i)];
                    color += doLightShadowed(light, in_pos, V, N, waterSpec, matColOverPi, 0.0, lightRough, lightRoughSq);
                }
                break;
            }
            tableIdx = getNextTableIdx(tableIdx);
        }
    }

    out_color = vec4(color, 1.0);
}
