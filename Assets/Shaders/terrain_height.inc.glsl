// Camera-centered terrain data cascades (CPU-baked by Procedural::TerrainStreamer, uploaded via
// Renderer::setFogTerrainHeightMap): an RGBA32F 2D array, layer 0 = near/fine cascade, layer 1 = far/
// coarse cascade (same resolution over a much larger range), one shared world center. Per texel:
//   R = raw terrain surface height (world Y, m)              — bilinear-safe
//   G = water surface level (world Y, m; sea level over open ocean, higher over lakes/rivers) — bilinear-safe
//   B = PACKED 4x8 bits BIT-CAST into the float (NaN patterns possible — floatBitsToUint only, never
//       float arithmetic or hardware filtering on the raw value):
//         bits 0-7   fog thickness, decodes to [0,1]
//         bits 8-15  fog height-falloff multiplier, decodes to [0,4] (1 = global falloff unchanged)
//         bits 16-23 temperature, decodes to CELSIUS over [-25, +50]
//         bits 24-31 humidity, decodes to [0,1] (0 -> 0.0, 255 -> 1.0)
//       Read via terrainClimateAt (manual bilinear of decoded texels) / terrainClimateNearestAt.
//   A = MACRO ALTITUDE (m above sea level, pass-1.5 field) — the terrain coloring splits "mountain"
//       (height far above it) from "high-altitude flatland" (height ~ altitude); bilinear-safe
// Consumers: the volumetric fog's terrain-following height base + regional density
// (vol_scatter.cs.glsl) and the ocean's depth/water-level fallback outside its own shore map
// (ocean_wave.inc.glsl).
//
// UBO packing (requires ubo.inc.glsl):
//   u_fogParams5.xy = world center XZ, u_fogParams3.y = 1 / near cascade world size (0 = no map),
//   u_fogParams5.z  = 1 / far cascade world size (0 = near only), u_fogParams5.w = baked terrain sea level
//
// The includer defines TERRAIN_HEIGHT_BINDING before including to bind the sampler.

#ifndef TERRAIN_HEIGHT_INC_GLSL
#define TERRAIN_HEIGHT_INC_GLSL

layout (binding = TERRAIN_HEIGHT_BINDING) uniform sampler2DArray u_terrainHeight;

bool terrainHeightMapPresent() { return u_fogParams3.y > 0.0; }

// Full texel (height, water level, fog thickness, spare) at worldXZ; fades to the far cascade over the
// near map's outer band so the handover never shows a seam. Beyond the far cascade, clamp-to-edge extends
// the edge values. Only meaningful while terrainHeightMapPresent().
vec4 terrainDataAt(vec2 worldXZ)
{
    const vec2 rel = worldXZ - u_fogParams5.xy;
    const vec2 uv0 = rel * u_fogParams3.y + 0.5;
    const float edge = max(abs(uv0.x - 0.5), abs(uv0.y - 0.5));
    const float nearW = u_fogParams5.z > 0.0 ? 1.0 - smoothstep(0.42, 0.48, edge) : 1.0;
    vec4 d = vec4(0.0);
    if (nearW < 1.0)
        d = textureLod(u_terrainHeight, vec3(rel * u_fogParams5.z + 0.5, 1.0), 0.0);
    if (nearW > 0.0)
        d = mix(d, textureLod(u_terrainHeight, vec3(uv0, 0.0), 0.0), nearW);
    return d;
}

// Raw terrain surface height (world Y, m) at worldXZ.
float terrainHeightAt(vec2 worldXZ) { return terrainDataAt(worldXZ).x; }

// One decoded climate texel from the bit-packed B channel at integer texel coords:
//   x = fog thickness [0,1], y = fog height-falloff multiplier [0,4] (1 = neutral),
//   z = temperature in CELSIUS [-25, +50], w = humidity [0,1].
vec4 terrainClimateTexel(ivec2 t, int layer, ivec2 res)
{
    const uint packed = floatBitsToUint(texelFetch(u_terrainHeight, ivec3(clamp(t, ivec2(0), res - 1), layer), 0).z);
    return vec4(float(packed & 255u) * (1.0 / 255.0),
                float((packed >> 8) & 255u) * (4.0 / 255.0),
                float((packed >> 16) & 255u) * (75.0 / 255.0) - 25.0,
                float((packed >> 24) & 255u) * (1.0 / 255.0));
}

// Manual bilinear of one cascade's climate: hardware filtering would blend the PACKED bits into
// garbage, so decode the 4 neighboring texels and blend the decoded values.
vec4 terrainClimateBilinear(vec2 uv, int layer, ivec2 res)
{
    const vec2 st = uv * vec2(res) - 0.5;
    const vec2 f = fract(st);
    const ivec2 b = ivec2(floor(st));
    const vec4 c00 = terrainClimateTexel(b, layer, res);
    const vec4 c10 = terrainClimateTexel(b + ivec2(1, 0), layer, res);
    const vec4 c01 = terrainClimateTexel(b + ivec2(0, 1), layer, res);
    const vec4 c11 = terrainClimateTexel(b + ivec2(1, 1), layer, res);
    return mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
}

// Climate fields at worldXZ, SMOOTH: manual bilinear + the same near/far cascade blend as terrainDataAt,
// so temperature/humidity gradients have the same effective resolution as the height channel (terrain
// coloring showed the raw texels as visible pixels at climate borders otherwise).
vec4 terrainClimateAt(vec2 worldXZ)
{
    const vec2 rel = worldXZ - u_fogParams5.xy;
    const ivec2 res = textureSize(u_terrainHeight, 0).xy;
    const vec2 uv0 = rel * u_fogParams3.y + 0.5;
    const float edge = max(abs(uv0.x - 0.5), abs(uv0.y - 0.5));
    const float nearW = u_fogParams5.z > 0.0 ? 1.0 - smoothstep(0.42, 0.48, edge) : 1.0;
    vec4 c = vec4(0.0);
    if (nearW < 1.0)
        c = terrainClimateBilinear(rel * u_fogParams5.z + 0.5, 1, res);
    if (nearW > 0.0)
        c = mix(c, terrainClimateBilinear(uv0, 0, res), nearW);
    return c;
}

// Cheap nearest-texel variant for volume consumers (froxel fog): regional fog is km-scale, texel
// granularity never shows there, and the froxel grid samples this a million times a frame.
vec4 terrainClimateNearestAt(vec2 worldXZ)
{
    const vec2 rel = worldXZ - u_fogParams5.xy;
    const vec2 uv0 = rel * u_fogParams3.y + 0.5;
    const float edge = max(abs(uv0.x - 0.5), abs(uv0.y - 0.5));
    const bool useFar = u_fogParams5.z > 0.0 && edge > 0.45;
    const vec2 uv = useFar ? rel * u_fogParams5.z + 0.5 : uv0;
    const ivec2 res = textureSize(u_terrainHeight, 0).xy;
    return terrainClimateTexel(ivec2(uv * vec2(res)), useFar ? 1 : 0, res);
}

#endif
