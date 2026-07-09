// Camera-centered terrain height cascades (CPU-baked by Procedural::TerrainStreamer, uploaded via
// Renderer::setFogTerrainHeightMap): an R32F 2D array, layer 0 = near/fine cascade, layer 1 = far/coarse
// cascade (same resolution over a much larger range), one shared world center. Consumers: the volumetric
// fog's terrain-following height base (vol_scatter.cs.glsl) and the ocean's water-depth fallback outside
// its own shore map (ocean_wave.inc.glsl).
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

// Raw terrain surface height (world Y, m) at worldXZ; fades to the far cascade over the near map's outer
// band so the handover never shows a seam. Beyond the far cascade, clamp-to-edge extends the edge heights.
// Only meaningful while terrainHeightMapPresent().
float terrainHeightAt(vec2 worldXZ)
{
    const vec2 rel = worldXZ - u_fogParams5.xy;
    const vec2 uv0 = rel * u_fogParams3.y + 0.5;
    const float edge = max(abs(uv0.x - 0.5), abs(uv0.y - 0.5));
    const float nearW = u_fogParams5.z > 0.0 ? 1.0 - smoothstep(0.42, 0.48, edge) : 1.0;
    float h = 0.0;
    if (nearW < 1.0)
        h = textureLod(u_terrainHeight, vec3(rel * u_fogParams5.z + 0.5, 1.0), 0.0).r;
    if (nearW > 0.0)
        h = mix(h, textureLod(u_terrainHeight, vec3(uv0, 0.0), 0.0).r, nearW);
    return h;
}

#endif
