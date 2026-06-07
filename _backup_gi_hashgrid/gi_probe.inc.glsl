// GI irradiance probe grid: a persistent world-space hash grid (sibling of the light grid) that stores
// one SH-L1 RGB irradiance probe per 4-unit cell. Each occupied 32-unit grid owns a dense 8x8x8 block of
// probes. Topology reuses the shared hashing (hash_grid.inc.glsl); only the per-grid payload differs.
//
// Buffers / names the includer must define before including:
//   PROBE_TABLE_NAME        uint[]  open-addressed table: tableIdx -> grid slot (EMPTY_ENTRY if free)
//   PROBE_GRID_DATA_NAME    uint[]  per-slot header: [slot*4 + 0..2] = gridMin (ivec3 bits), [+3] reserved
//   PROBE_SH_NAME           float[] SH coefficients: [(slot*GI_PROBES_PER_GRID + probe)*GI_SH_STRIDE + k]
// For the write side (alloc/trace) also define GI_PROBE_WRITE and:
//   PROBE_NUM_GRIDS_NAME    uint    slot allocation counter
//
// Requires shared.inc.glsl (EMPTY_ENTRY/INITIALIZING_ENTRY, PI) and includes hash_grid.inc.glsl.
// The GI_* constants below must match RendererVKLayout (Layout.ixx).

#ifndef GI_PROBE_INC_GLSL
#define GI_PROBE_INC_GLSL

#include "hash_grid.inc.glsl"

#define GI_PROBES_PER_AXIS 8
#define GI_PROBES_PER_GRID 512                                       // 8^3
#define GI_PROBE_SPACING   (float(GRID_SIZE) / float(GI_PROBES_PER_AXIS)) // 4.0 world units
#define GI_SH_STRIDE       12                                        // SH-L1 RGB: 4 coeffs * 3 channels
#define GI_MAX_PROBE_GRIDS 2048u
#define GI_PROBE_TABLE_SIZE 4096u                                    // power of two, > 2 * GI_MAX_PROBE_GRIDS

// SH-L1 basis for the four coefficients (l=0, then l=1 m=-1,0,+1 -> y,z,x).
vec4 shBasisL1(vec3 d)
{
    return vec4(0.282095, 0.488603 * d.y, 0.488603 * d.z, 0.488603 * d.x);
}

uint probeFindSlot(ivec3 gp)
{
    uint idx = hashTableIndex(gp, GI_PROBE_TABLE_SIZE);
    for (uint i = 0u; i < GI_PROBE_TABLE_SIZE; ++i)
    {
        uint slot = PROBE_TABLE_NAME[idx];
        if (slot == EMPTY_ENTRY)
            return EMPTY_ENTRY;
        if (slot < GI_MAX_PROBE_GRIDS)
        {
            ivec3 gm = ivec3(PROBE_GRID_DATA_NAME[slot * 4u + 0u], PROBE_GRID_DATA_NAME[slot * 4u + 1u], PROBE_GRID_DATA_NAME[slot * 4u + 2u]);
            if (gm == gp)
                return slot;
        }
        idx = hashNextIndex(idx, GI_PROBE_TABLE_SIZE);
    }
    return EMPTY_ENTRY;
}

// Cosine-convolved irradiance E(n) reconstructed from one probe's SH-L1. Diffuse exit radiance is
// albedo/PI * E(n).
vec3 evalProbeSlot(uint slot, uint probeIdx, vec3 n)
{
    uint base = (slot * GI_PROBES_PER_GRID + probeIdx) * GI_SH_STRIDE;
    vec3 c0 = vec3(PROBE_SH_NAME[base + 0u], PROBE_SH_NAME[base + 1u], PROBE_SH_NAME[base + 2u]);
    vec3 c1 = vec3(PROBE_SH_NAME[base + 3u], PROBE_SH_NAME[base + 4u], PROBE_SH_NAME[base + 5u]);
    vec3 c2 = vec3(PROBE_SH_NAME[base + 6u], PROBE_SH_NAME[base + 7u], PROBE_SH_NAME[base + 8u]);
    vec3 c3 = vec3(PROBE_SH_NAME[base + 9u], PROBE_SH_NAME[base + 10u], PROBE_SH_NAME[base + 11u]);
    vec4 Y = shBasisL1(n);
    const float A0 = PI;             // cosine-lobe zonal harmonics (irradiance convolution)
    const float A1 = 2.0 * PI / 3.0;
    vec3 E = A0 * c0 * Y.x + A1 * (c1 * Y.y + c2 * Y.z + c3 * Y.w);
    return max(E, vec3(0.0));
}

// Trilinear-interpolated irradiance from the 8 nearest probes (each looked up by its own grid, so it
// works across 32-unit grid boundaries). Returns vec3(-1) when no probe data exists nearby.
vec3 evalProbeSH(vec3 worldPos, vec3 n)
{
    vec3 p = worldPos + n * (GI_PROBE_SPACING * 0.5); // push along the normal to limit self-leak
    vec3 pf = p / GI_PROBE_SPACING - 0.5;             // probe centers sit on integer coords here
    ivec3 baseCoord = ivec3(floor(pf));
    vec3 frac = pf - vec3(baseCoord);

    vec3 result = vec3(0.0);
    float totalW = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        ivec3 off = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        vec3 w3 = mix(1.0 - frac, frac, vec3(off));
        float w = w3.x * w3.y * w3.z;
        if (w <= 0.0)
            continue;

        ivec3 probeCoord = baseCoord + off;
        vec3 probeWorld = (vec3(probeCoord) + 0.5) * GI_PROBE_SPACING;
        ivec3 gp = getGridPos(probeWorld);
        uint slot = probeFindSlot(gp);
        if (slot == EMPTY_ENTRY)
            continue;

        ivec3 localProbe = probeCoord - gp * GI_PROBES_PER_AXIS;
        uint probeIdx = uint(localProbe.x + localProbe.y * GI_PROBES_PER_AXIS + localProbe.z * GI_PROBES_PER_AXIS * GI_PROBES_PER_AXIS);
        result += w * evalProbeSlot(slot, probeIdx, n);
        totalW += w;
    }
    if (totalW > 1e-4)
        return result / totalW;
    return vec3(-1.0);
}

#ifdef GI_PROBE_WRITE

// Insert (or find) the grid at gp, returning its slot. Mirrors the light grid's getOrInsertGrid:
// open-addressed claim of an EMPTY table entry, slot allocated from PROBE_NUM_GRIDS_NAME. Returns
// EMPTY_ENTRY if the grid capacity is exhausted.
uint getOrInsertProbeGrid(ivec3 gp)
{
    uint idx = hashTableIndex(gp, GI_PROBE_TABLE_SIZE);
    uint guard = 0u;
    while (guard < GI_PROBE_TABLE_SIZE)
    {
        memoryBarrierBuffer();
        uint slot = PROBE_TABLE_NAME[idx];
        if (slot < GI_MAX_PROBE_GRIDS)
        {
            ivec3 gm = ivec3(PROBE_GRID_DATA_NAME[slot * 4u + 0u], PROBE_GRID_DATA_NAME[slot * 4u + 1u], PROBE_GRID_DATA_NAME[slot * 4u + 2u]);
            if (gm == gp)
                return slot;
            idx = hashNextIndex(idx, GI_PROBE_TABLE_SIZE);
            guard++;
        }
        else if (atomicCompSwap(PROBE_TABLE_NAME[idx], EMPTY_ENTRY, INITIALIZING_ENTRY) == EMPTY_ENTRY)
        {
            uint newSlot = atomicAdd(PROBE_NUM_GRIDS_NAME, 1u);
            if (newSlot >= GI_MAX_PROBE_GRIDS)
            {
                atomicExchange(PROBE_TABLE_NAME[idx], EMPTY_ENTRY); // capacity exhausted: release the entry
                return EMPTY_ENTRY;
            }
            PROBE_GRID_DATA_NAME[newSlot * 4u + 0u] = uint(gp.x);
            PROBE_GRID_DATA_NAME[newSlot * 4u + 1u] = uint(gp.y);
            PROBE_GRID_DATA_NAME[newSlot * 4u + 2u] = uint(gp.z);
            PROBE_GRID_DATA_NAME[newSlot * 4u + 3u] = 0u;
            memoryBarrierBuffer();
            atomicExchange(PROBE_TABLE_NAME[idx], newSlot);
            return newSlot;
        }
        // else: another invocation is initializing this entry; spin and re-read.
    }
    return EMPTY_ENTRY;
}

// Temporally blend this frame's freshly-projected coefficients into a probe (lerp toward the new value).
void probeBlendSH(uint slot, uint probeIdx, vec3 c0, vec3 c1, vec3 c2, vec3 c3, float alpha)
{
    uint base = (slot * GI_PROBES_PER_GRID + probeIdx) * GI_SH_STRIDE;
    PROBE_SH_NAME[base + 0u]  = mix(PROBE_SH_NAME[base + 0u],  c0.r, alpha);
    PROBE_SH_NAME[base + 1u]  = mix(PROBE_SH_NAME[base + 1u],  c0.g, alpha);
    PROBE_SH_NAME[base + 2u]  = mix(PROBE_SH_NAME[base + 2u],  c0.b, alpha);
    PROBE_SH_NAME[base + 3u]  = mix(PROBE_SH_NAME[base + 3u],  c1.r, alpha);
    PROBE_SH_NAME[base + 4u]  = mix(PROBE_SH_NAME[base + 4u],  c1.g, alpha);
    PROBE_SH_NAME[base + 5u]  = mix(PROBE_SH_NAME[base + 5u],  c1.b, alpha);
    PROBE_SH_NAME[base + 6u]  = mix(PROBE_SH_NAME[base + 6u],  c2.r, alpha);
    PROBE_SH_NAME[base + 7u]  = mix(PROBE_SH_NAME[base + 7u],  c2.g, alpha);
    PROBE_SH_NAME[base + 8u]  = mix(PROBE_SH_NAME[base + 8u],  c2.b, alpha);
    PROBE_SH_NAME[base + 9u]  = mix(PROBE_SH_NAME[base + 9u],  c3.r, alpha);
    PROBE_SH_NAME[base + 10u] = mix(PROBE_SH_NAME[base + 10u], c3.g, alpha);
    PROBE_SH_NAME[base + 11u] = mix(PROBE_SH_NAME[base + 11u], c3.b, alpha);
}

#endif // GI_PROBE_WRITE

#endif // GI_PROBE_INC_GLSL
