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

// Directional reach shaping — a smooth CONE, so a focused emitter reads as a spear/triangle: the
// silhouette runs a STRAIGHT edge from the tip at Reach*(1+f) forward to the base (half-width
// Reach) at the emitter plane, with a spherical cap behind. Polar cone: r(theta) = T/(c + T*s).
// (The previous pow() lobe was flat over the whole back hemisphere and superlinear at the front —
// a back bulb + waist + fat forward blob, not a spear.) The smooth-relu of c keeps it C1; focus 0
// stays an exact sphere (cone blended in over f < 0.5).
float forceLobe(float c, float f)
{
    const float T = 1.0 + f;
    // EXACT sine in the cone term — deriving it from a smoothed cosine pinned it at a floor near
    // the axis, which flattened the last degrees of taper into a constant-radius tube with a flat
    // cap (jar neck). The cap blend below is the only smoothing the junction needs.
    const float s = sqrt(max(1.0 - c * c, 0.0));
    const float cone = T / (max(c, 0.0) + T * max(s, 1e-4));
    const float L = mix(1.0, cone, smoothstep(-0.1, 0.1, c)); // spherical cap behind -> cone ahead
    return mix(1.0, L, min(f * 2.0, 1.0));
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
// interval, and the grid insert so they always agree. Local frame: +Z = emitter direction. The
// cone lobe never exceeds Reach laterally or behind (base half-width = Reach at the emitter
// plane, spherical cap behind), so the box is simply Reach sideways/back (+5% slack) and
// Reach*(1+f) forward.
void forceEmitterBounds(ForceEmitterData e, out float side, out float forward, out float back)
{
    const float f = e.dirFocus.w;
    const float reach = e.posReach.w;
    side = reach * 1.05;
    forward = reach * (1.0 + f);
    back = reach * 1.05;
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

// C1 smooth max (k = blend width; 0 = hard max). Used for the surface's opposing bound so the
// shells meet the equilibrium wall in a rounded fillet instead of a hard crease — the crease's
// discontinuous normals printed march-step-sized classification jaggies along the junction.
float forceSmoothMax(float a, float b, float k)
{
    const float h = max(k - abs(a - b), 0.0) / max(k, 1e-5);
    return max(a, b) + h * h * k * 0.25;
}

// The bound a team's field must beat to be "inside": iso or the strongest opposing field, smoothly
// blended (u_forceParams3.w = junction smoothing as a fraction of iso; scale-free since the rim
// region lives at field values ~iso). The query compute uses the same function, so gameplay
// "inside" matches the drawn surface exactly.
float forceOpposingBound(float iso, float opposing)
{
    return forceSmoothMax(iso, opposing, u_forceParams3.w * iso);
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
    return phi[team] - forceOpposingBound(iso, opposing);
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
    F = bestPhi - forceOpposingBound(iso, secondPhi);
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

// Signed field difference between two teams: positive on teamA's side, zero exactly on the
// equilibrium WALL between two pressed bubbles. F itself only dips to 0 there without changing
// sign (best/second swap), so the interior wall must be detected and refined on THIS function.
float forceTeamDiff(vec3 x, uint teamA, uint teamB)
{
    float phi[MAX_FORCE_TEAMS];
    forceAccumulate(x, phi);
    return phi[teamA] - phi[teamB];
}

// Normal of the equilibrium wall (gradient of the team difference), pointing toward teamA's side.
vec3 forceWallNormal(vec3 x, uint teamA, uint teamB, float h)
{
    const vec2 k = vec2(1.0, -1.0);
    vec3 grad = k.xyy * forceTeamDiff(x + k.xyy * h, teamA, teamB)
              + k.yyx * forceTeamDiff(x + k.yyx * h, teamA, teamB)
              + k.yxy * forceTeamDiff(x + k.yxy * h, teamA, teamB)
              + k.xxx * forceTeamDiff(x + k.xxx * h, teamA, teamB);
    return normalize(grad + vec3(0.0, 1e-6, 0.0));
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
