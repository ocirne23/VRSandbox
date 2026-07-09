// Camera-centered terrain data cascades (CPU-baked by Procedural::TerrainStreamer, uploaded via
// Renderer::setFogTerrainHeightMap): an RGBA32F 2D array, layer 0 = near/fine cascade, layer 1 = far/
// coarse cascade (same resolution over a much larger range), one shared world center. Per texel:
//   R = raw terrain surface height (world Y, m)              — bilinear-safe
//   G = water surface level (world Y, m; sea level over open ocean, higher over lakes/rivers) — bilinear-safe
//   B = PACKED 3x8 bits: fog thickness | temperature << 8 | humidity << 16 (all [0,1] quantized).
//       Bit-packed floats cannot be bilinearly filtered — read via terrainClimateAt (nearest texel).
//   A = spare (0) for a future field
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

// Climate fields (fog thickness, temperature, humidity), each [0,1], unpacked from the bit-packed B
// channel — nearest-texel (bilinear would blend the packed integers into garbage). Cascade selection
// mirrors terrainDataAt but switches hard at the blend midpoint; these fields are low-frequency, so
// texel granularity is invisible where they're consumed (froxel fog, biome tints).
vec3 terrainClimateAt(vec2 worldXZ)
{
    const vec2 rel = worldXZ - u_fogParams5.xy;
    const vec2 uv0 = rel * u_fogParams3.y + 0.5;
    const float edge = max(abs(uv0.x - 0.5), abs(uv0.y - 0.5));
    const bool useFar = u_fogParams5.z > 0.0 && edge > 0.45;
    const vec2 uv = useFar ? rel * u_fogParams5.z + 0.5 : uv0;
    const ivec2 res = textureSize(u_terrainHeight, 0).xy;
    const ivec2 texel = clamp(ivec2(uv * vec2(res)), ivec2(0), res - 1);
    const uint packed = uint(texelFetch(u_terrainHeight, ivec3(texel, useFar ? 1 : 0), 0).z);
    return vec3(float(packed & 255u), float((packed >> 8) & 255u), float((packed >> 16) & 255u)) * (1.0 / 255.0);
}

#endif
