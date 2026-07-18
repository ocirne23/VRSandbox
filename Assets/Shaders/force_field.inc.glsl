// Forcefield bubble field math — THE single definition, consumed by the shell draw
// (force_shell.vs/fs.glsl) and the grid/force/query compute passes. Keep the lobe in sync with the
// CPU mirror in Code/Force/Private/System.cpp (forceLobe) — the debug rings and gameplay reason
// about the same field the pixels shade.
//
// Emitter contribution at x (COMPACT SUPPORT — exactly 0 beyond the directional reach):
//   dHat = normalize(x - pos), c = dot(dHat, dir)
//   L(c, f) = mix(1/(1 + 0.5f), 1 + f, pow(max(c, 0), 2 + f))   // focus lobe; f = 0 -> sphere
//   Reff = Reach * L;  u = r / Reff
//   w = Output * (1 - u^2)^2   for u < 1, else 0                 // quartic falloff, C1 at the edge
// A team's field phi[t] is the SUM of its emitters' w (merging). A point is inside team t where
// phi[t] > iso AND phi[t] beats every other team; the drawn surface is
//   F = phi[best] - max(iso, phi[secondBest]) = 0
// — the equal-field pressure equilibrium, so squish and focused-lobe pierce need no simulation.
//
// This include declares the compacted emitter buffer itself; the consumer sets
// FORCE_EMITTERS_BINDING before including (default 1). Candidate enumeration: with FORCE_GRID
// defined, evaluation gathers the point's hash-grid cell + the globally-scanned big-emitter list
// (force_grid.inc.glsl); without it, every evaluation scans all fe_count emitters (small scenes /
// A-B correctness toggle).

#ifndef FORCE_FIELD_INC_GLSL
#define FORCE_FIELD_INC_GLSL

#ifndef FORCE_EMITTERS_BINDING
#define FORCE_EMITTERS_BINDING 1
#endif

struct ForceEmitterData
{
    vec4 posReach;     // xyz = world position, w = Reach (m)
    vec4 dirFocus;     // xyz = normalized direction, w = focus >= 0 (0 = sphere)
    vec4 outputParams; // x = Output (field strength), y = shell alpha mult, zw unused
    uvec4 teamFlags;   // x = team [0, MAX_FORCE_TEAMS), y = FORCE_FLAG_* bits, z = source slot (readback), w unused
};

// Matches RendererVKLayout::ForceEmittersGpu (header + big-emitter index list + compacted live emitters).
layout (binding = FORCE_EMITTERS_BINDING, std430) readonly buffer ForceEmitters
{
    uint fe_count; uint fe_bigCount; uint fe_pad0; uint fe_pad1;
    uint fe_bigIndices[MAX_FORCE_BIG_EMITTERS];
    ForceEmitterData fe_emitters[];
};

#ifdef FORCE_GRID
#include "force_grid.inc.glsl"
#endif

// Directional reach shaping. C1 in c (exponent >= 2), so field surfaces stay smooth.
float forceLobe(float c, float f)
{
    const float side = 1.0 / (1.0 + 0.5 * f);
    return mix(side, 1.0 + f, pow(max(c, 0.0), 2.0 + f));
}

// One emitter's field contribution at x.
float forceContribution(vec3 x, ForceEmitterData e)
{
    const vec3 d = x - e.posReach.xyz;
    const float r2 = dot(d, d);
    const float maxReach = e.posReach.w * (1.0 + e.dirFocus.w);
    if (r2 >= maxReach * maxReach)
        return 0.0;
    const float r = sqrt(max(r2, 1e-8));
    const float c = dot(d / r, e.dirFocus.xyz);
    const float reff = e.posReach.w * forceLobe(c, e.dirFocus.w);
    const float u2 = r2 / (reff * reff);
    if (u2 >= 1.0)
        return 0.0;
    const float q = 1.0 - u2;
    return e.outputParams.x * q * q;
}

// Conservative local bounds of an emitter's field support, shared by the proxy VS, the FS ray
// interval, and the grid insert so they always agree. Local frame: +Z = emitter direction.
//   forward = Reach * (1 + f); back = Reach * side lobe; side = Reach * max lateral extent of the
// lobe (analytic optimum of sin(theta) * L(cos theta), + 5% slack).
void forceEmitterBounds(ForceEmitterData e, out float side, out float forward, out float back)
{
    const float f = e.dirFocus.w;
    const float reach = e.posReach.w;
    const float s = 1.0 / (1.0 + 0.5 * f);
    const float p = 2.0 + f;
    const float a = p / (p + 1.0);
    const float m = sqrt(1.0 - a) * pow(a, p * 0.5); // max of sin(t)*cos(t)^p
    side = reach * min(s + (1.0 + f - s) * m, 1.0 + f) * 1.05;
    forward = reach * (1.0 + f);
    back = reach * s;
}

// Orthonormal RIGHT-HANDED frame with +Z = dir (right x up == dir). Handedness is load-bearing: a
// mirrored basis flips the proxy cube's winding, so front-face culling keeps the NEAR faces instead
// of the far ones and the shell stops rasterizing entirely with the camera inside the box.
mat3 forceEmitterBasis(vec3 dir)
{
    const vec3 ref = abs(dir.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    const vec3 right = normalize(cross(dir, ref));
    const vec3 up = cross(dir, right);
    return mat3(right, up, dir);
}

// ---- candidate enumeration: every field evaluation loops the same way ----
//   const uint cell = forceCandidateCell(x);
//   const uint n = forceNumCandidates(cell);
//   for (uint k = 0u; k < n; ++k) { e = fe_emitters[forceCandidateIdx(cell, k)]; ... }

#ifdef FORCE_GRID
uint forceCandidateCell(vec3 x) { return forceFindCell(getGridPos(x)); }
uint forceNumCandidates(uint cell)
{
    return fe_bigCount + (cell != FORCE_INVALID_CELL ? forceCellCount(cell) : 0u);
}
uint forceCandidateIdx(uint cell, uint k)
{
    return k < fe_bigCount ? fe_bigIndices[k] : forceCellEmitter(cell, k - fe_bigCount);
}
#else
uint forceCandidateCell(vec3 x) { return 0u; }
uint forceNumCandidates(uint cell) { return fe_count; }
uint forceCandidateIdx(uint cell, uint k) { return k; }
#endif

// Per-team field accumulation at x. MAX_FORCE_TEAMS is injected from Layout.ixx (= 8).
void forceAccumulate(vec3 x, out float phi[MAX_FORCE_TEAMS])
{
    for (uint t = 0u; t < MAX_FORCE_TEAMS; ++t)
        phi[t] = 0.0;
    const uint cell = forceCandidateCell(x);
    const uint n = forceNumCandidates(cell);
    for (uint k = 0u; k < n; ++k)
    {
        const ForceEmitterData e = fe_emitters[forceCandidateIdx(cell, k)];
        phi[e.teamFlags.x] += forceContribution(x, e);
    }
}

// The render surface function F for a FIXED team t (marched/refined/differentiated holding the hit
// team constant): positive inside t's bubble, zero on its surface.
float forceSurfaceForTeam(vec3 x, uint team, float iso)
{
    float phi[MAX_FORCE_TEAMS];
    forceAccumulate(x, phi);
    float opposing = 0.0;
    for (uint t = 0u; t < MAX_FORCE_TEAMS; ++t)
        if (t != team)
            opposing = max(opposing, phi[t]);
    return phi[team] - max(iso, opposing);
}

// A fixed team's own field and its best opposing field at x.
void forceTeamSample(vec3 x, uint team, out float own, out float opposing)
{
    float phi[MAX_FORCE_TEAMS];
    forceAccumulate(x, phi);
    own = phi[team];
    opposing = 0.0;
    for (uint t = 0u; t < MAX_FORCE_TEAMS; ++t)
        if (t != team)
            opposing = max(opposing, phi[t]);
}

// Full sample: strongest team, its field, and the best opposing field. F > 0 means x is inside
// bestTeam's bubble.
void forceSampleField(vec3 x, float iso, out uint bestTeam, out float bestPhi, out float secondPhi, out float F)
{
    float phi[MAX_FORCE_TEAMS];
    forceAccumulate(x, phi);
    bestTeam = 0u;
    bestPhi = phi[0];
    for (uint t = 1u; t < MAX_FORCE_TEAMS; ++t)
        if (phi[t] > bestPhi) { bestPhi = phi[t]; bestTeam = t; }
    secondPhi = 0.0;
    for (uint t = 0u; t < MAX_FORCE_TEAMS; ++t)
        if (t != bestTeam)
            secondPhi = max(secondPhi, phi[t]);
    F = bestPhi - max(iso, secondPhi);
}

// The strongest single contributor of `team` at x — the OWNER of a shell surface point. Every proxy
// marches its own box, so overlapping same-team proxies would shade a merged surface point once per
// proxy; only the fragment whose instance IS the dominant contributor keeps it (compact support puts
// the point inside the dominant emitter's own proxy, so exactly one fragment survives). Ties break
// to the lowest compact index.
uint forceDominantEmitter(vec3 x, uint team)
{
    uint bestIdx = 0xFFFFFFFFu;
    float bestW = -1.0;
    const uint cell = forceCandidateCell(x);
    const uint n = forceNumCandidates(cell);
    for (uint k = 0u; k < n; ++k)
    {
        const uint i = forceCandidateIdx(cell, k);
        if (fe_emitters[i].teamFlags.x != team)
            continue;
        const float w = forceContribution(x, fe_emitters[i]);
        if (w > bestW || (w == bestW && i < bestIdx))
        {
            bestW = w;
            bestIdx = i;
        }
    }
    return bestIdx;
}

// Outward surface normal at a hit via 4-tap tetrahedral finite differences of F (team held fixed).
// F increases inward, so the outward normal is the negated gradient. h scales with the dominant
// emitter's reach so tiny bubbles and huge domes both resolve.
vec3 forceSurfaceNormal(vec3 x, uint team, float iso, float h)
{
    const vec2 k = vec2(1.0, -1.0);
    vec3 grad = k.xyy * forceSurfaceForTeam(x + k.xyy * h, team, iso)
              + k.yyx * forceSurfaceForTeam(x + k.yyx * h, team, iso)
              + k.yxy * forceSurfaceForTeam(x + k.yxy * h, team, iso)
              + k.xxx * forceSurfaceForTeam(x + k.xxx * h, team, iso);
    return -normalize(grad + vec3(0.0, 1e-6, 0.0));
}

// Analytic spherical-approximation gradient of the STRONGEST opposing team's field (treats Reff as
// locally constant per emitter — exact at focus 0, directionally correct enough for gameplay
// knockback). Also returns that team's field strength (the "pressure" scalar). Compute passes only.
vec3 forceOpposingGradient(vec3 x, uint team, out float opposingPhi)
{
    float phi[MAX_FORCE_TEAMS];
    vec3 gradPerTeam[MAX_FORCE_TEAMS];
    for (uint t = 0u; t < MAX_FORCE_TEAMS; ++t)
    {
        phi[t] = 0.0;
        gradPerTeam[t] = vec3(0.0);
    }
    const uint cell = forceCandidateCell(x);
    const uint n = forceNumCandidates(cell);
    for (uint k = 0u; k < n; ++k)
    {
        const ForceEmitterData e = fe_emitters[forceCandidateIdx(cell, k)];
        if (e.teamFlags.x == team)
            continue;
        const vec3 d = x - e.posReach.xyz;
        const float r2 = dot(d, d);
        const float r = sqrt(max(r2, 1e-8));
        const float c = dot(d / r, e.dirFocus.xyz);
        const float reff = e.posReach.w * forceLobe(c, e.dirFocus.w);
        const float u2 = r2 / (reff * reff);
        if (u2 >= 1.0)
            continue;
        const float q = 1.0 - u2;
        phi[e.teamFlags.x] += e.outputParams.x * q * q;
        // dw/dr = -4 * Output * u * (1 - u^2) / Reff, along dHat
        gradPerTeam[e.teamFlags.x] += (-4.0 * e.outputParams.x * sqrt(u2) * q / reff) * (d / r);
    }
    opposingPhi = 0.0;
    vec3 grad = vec3(0.0);
    for (uint t = 0u; t < MAX_FORCE_TEAMS; ++t)
    {
        if (t != team && phi[t] > opposingPhi)
        {
            opposingPhi = phi[t];
            grad = gradPerTeam[t];
        }
    }
    return grad;
}

#endif
