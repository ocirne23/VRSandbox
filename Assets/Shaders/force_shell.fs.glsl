#version 460

// Forcefield shell fragment shader: ray-marches the analytic team field through this instance's
// oriented reach box and composites up to TWO surface crossings of F = phi[best] - max(iso,
// phi[second]) front-to-back — the front shell plus the far/inner shell behind it (its visibility
// is the "Backface alpha" tweak; from inside a bubble the exit dome uses "Interior alpha" instead).
// Each crossing is shaded as a fresnel-rimmed energy shell (team color, world-anchored hex/noise
// pattern, contact glow where an opposing bubble presses in, soft glow where the shell meets scene
// geometry). Ownership discard keeps merged same-team bubbles single-shaded: a crossing is only
// shaded by the fragment whose instance is the DOMINANT contributor there; unowned crossings still
// advance the march (their owner's fragment draws them). Premultiplied blend over the lit scene,
// manual depth test against the G-buffer depth (reversed-Z), no depth write — particles and fog
// layer on top.

#include "shared.inc.glsl"
#include "force_field.inc.glsl" // declares the emitter buffer at FORCE_EMITTERS_BINDING (1)

layout (binding = 2) uniform sampler2D u_gbufferDepth;

layout (push_constant) uniform ViewPC { uint u_viewIndex; };

layout (location = 0) in flat uint v_emitterIdx;

layout (location = 0) out vec4 out_color;

// ---- animated surface pattern ----

float forceHash(vec3 p)
{
    uvec3 q = floatBitsToUint(p) * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
    n = n * 747796405u + 2891336453u;
    n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
    return float((n >> 22u) ^ n) * (1.0 / 1023.0 / 1024.0 / 1024.0);
}

float forceValueNoise(vec3 p)
{
    const vec3 i = floor(p);
    vec3 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(forceHash(i + vec3(0, 0, 0)), forceHash(i + vec3(1, 0, 0)), f.x),
                   mix(forceHash(i + vec3(0, 1, 0)), forceHash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(forceHash(i + vec3(0, 0, 1)), forceHash(i + vec3(1, 0, 1)), f.x),
                   mix(forceHash(i + vec3(0, 1, 1)), forceHash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

// Distance-to-hex-border in a pointy-top hex tiling: 0 at cell centers, 0.5 on the borders.
float forceHexDist(vec2 p)
{
    const vec2 r = vec2(1.0, 1.7320508);
    const vec2 a = mod(p, r) - r * 0.5;
    const vec2 b = mod(p - r * 0.5, r) - r * 0.5;
    const vec2 gv = dot(a, a) < dot(b, b) ? a : b;
    const vec2 q = abs(gv);
    return max(q.x * 0.8660254 + q.y * 0.5, q.y);
}

// Triplanar hexagonal energy pattern + drifting noise ripple, purely WORLD-anchored: moving emitters
// slide through a stable pattern, and merged same-team shells shade seamlessly across their
// ownership boundary (any per-emitter term here would print a seam where the dominant owner flips).
float forcePattern(vec3 worldPos, vec3 n)
{
    const float scale = u_forceParams2.x;
    const float t = u_timeSeconds * u_forceParams2.y;
    vec3 w = n * n;
    w *= w;
    w /= (w.x + w.y + w.z);
    const float hexX = forceHexDist(worldPos.yz * scale + t * 0.31);
    const float hexY = forceHexDist(worldPos.zx * scale + t * 0.27);
    const float hexZ = forceHexDist(worldPos.xy * scale + t * 0.23);
    const float hex = smoothstep(0.38, 0.5, hexX) * w.x
                    + smoothstep(0.38, 0.5, hexY) * w.y
                    + smoothstep(0.38, 0.5, hexZ) * w.z;
    const float ripple = forceValueNoise(worldPos * scale * 2.7 + vec3(0.0, t * 1.7, 0.0));
    return hex * (0.65 + 0.35 * ripple) + ripple * ripple * 0.35;
}

// Shades one refined surface crossing. Returns PREMULTIPLIED rgb + alpha, ready to composite
// front-to-back. Layer styling: front surfaces (outward normal toward the camera) are the standard
// shell; surfaces seen from their inside are the interior dome (camera within a bubble, "Interior
// alpha" floor) or the far/inner backface seen through the front ("Backface alpha" scale).
vec4 forceShadeHit(vec3 rayOrigin, vec3 rayDir, float tHit, uint hitTeam, bool cameraInsideField, float sceneDist)
{
    const vec3 hitPos = rayOrigin + rayDir * tHit;
    float ownPhi, opposingPhi;
    forceTeamSample(hitPos, hitTeam, ownPhi, opposingPhi);
    if (ownPhi <= 0.0)
        return vec4(0.0);

    const float iso = u_forceParams0.x;
    const float h = max(0.005 * fe_emitters[v_emitterIdx].posReach.w, 0.01);
    vec3 n = forceSurfaceNormal(hitPos, hitTeam, iso, h);
    const bool viewedFromInside = dot(n, rayDir) > 0.0;
    if (viewedFromInside)
        n = -n;

    const vec3 teamColor = u_forceTeamColors[hitTeam].rgb;
    const float fresnel = pow(1.0 - clamp(dot(n, -rayDir), 0.0, 1.0), u_forceParams0.y);
    // Contact glow: the equilibrium seam lights up as the best opposing field approaches our own.
    const float contact = smoothstep(1.0 - u_forceParams1.y, 1.0, opposingPhi / max(ownPhi, 1e-4));
    // Geometry glow: the shell surface fading into nearby opaque geometry along the view ray.
    const float geoGlow = u_forceParams1.z > 0.0
        ? 1.0 - clamp((sceneDist - tHit) / u_forceParams1.z, 0.0, 1.0) : 0.0;
    const float pattern = forcePattern(hitPos, n) * u_forceParams2.z;
    const float alphaMult = fe_emitters[v_emitterIdx].outputParams.y;

    const float rimI = u_forceParams0.z;
    vec3 color = teamColor * (rimI * fresnel + pattern * (0.25 + 0.75 * fresnel));
    color += mix(teamColor, vec3(1.0), 0.6) * contact * u_forceParams1.x;
    color += teamColor * geoGlow * rimI * 0.5;

    float alpha = u_forceParams0.w * alphaMult * (0.2 + 0.8 * fresnel);
    float layerScale = 1.0;
    if (viewedFromInside)
    {
        if (cameraInsideField)
        {
            // Interior dome: pattern-forward opacity floor, so the shell stays visible looking out
            // from within (rims still brighten toward grazing angles via fresnel).
            const float interior = u_forceParams3.x * alphaMult;
            color += teamColor * (0.3 + pattern) * interior;
            alpha = max(alpha, interior * (0.5 + 0.35 * pattern));
        }
        else
        {
            layerScale = u_forceParams3.y; // far/inner surface seen from outside, through the front
        }
    }
    alpha += contact * 0.25 + geoGlow * 0.1;
    alpha = clamp(alpha, 0.0, 1.0) * layerScale;
    // Premultiplied + a slight additive lift so rims/glow bloom over the scene.
    return vec4(color * alpha + color * 0.15 * layerScale, alpha);
}

void main()
{
    g_viewIndex = int(u_viewIndex);
    const ForceEmitterData e = fe_emitters[v_emitterIdx];
    const float iso = u_forceParams0.x;

    // View ray through this fragment (far-plane reconstruction; reversed-Z far = 0).
    const vec2 uv = gl_FragCoord.xy * u_screenSize.zw;
    const vec3 rayOrigin = u_viewPos;
    const vec3 rayDir = normalize(worldPosFromDepth(uv, 0.0) - rayOrigin);

    // Intersect the same oriented reach box the VS rasterized.
    float side, forward, back;
    forceEmitterBounds(e, side, forward, back);
    const mat3 basis = forceEmitterBasis(e.dirFocus.xyz);
    const vec3 center = e.posReach.xyz + e.dirFocus.xyz * (forward - back) * 0.5;
    const vec3 halfExtents = vec3(side, side, (forward + back) * 0.5);
    const vec3 localOrigin = transpose(basis) * (rayOrigin - center);
    const vec3 localDir = transpose(basis) * rayDir;
    const vec3 invDir = 1.0 / (localDir + vec3(equal(localDir, vec3(0.0))) * 1e-8);
    const vec3 tA = (-halfExtents - localOrigin) * invDir;
    const vec3 tB = ( halfExtents - localOrigin) * invDir;
    float t0 = max(max(min(tA.x, tB.x), min(tA.y, tB.y)), min(tA.z, tB.z));
    float t1 = min(min(max(tA.x, tB.x), max(tA.y, tB.y)), max(tA.z, tB.z));
    t0 = max(t0, 0.0);

    // Manual depth test: clamp the march to the opaque scene.
    const float sceneDepth = texture(u_gbufferDepth, uv).r;
    float sceneDist = 1e30;
    if (sceneDepth > 0.0) // reversed-Z: 0 = sky/far
    {
        sceneDist = distance(rayOrigin, worldPosFromDepth(uv, sceneDepth));
        t1 = min(t1, sceneDist);
    }
    if (t1 <= t0)
        discard;

    // Fixed-step march compositing up to two crossings of F (front shell + the surface behind it).
    const int steps = int(u_forceParams1.w);
    const float dt = (t1 - t0) / float(steps);
    uint bestTeam;
    float bestPhi, secondPhi, F;
    forceSampleField(rayOrigin + rayDir * t0, iso, bestTeam, bestPhi, secondPhi, F);
    // "Inside a bubble" is a property of the CAMERA, not of this box's entry point: a proxy whose
    // box begins inside the merged field must style the exit it finds as a backface, not a dome.
    bool cameraInsideField = F > 0.0;
    if (t0 > 0.0 && cameraInsideField)
    {
        uint originTeam;
        float originBest, originSecond, originF;
        forceSampleField(rayOrigin, iso, originTeam, originBest, originSecond, originF);
        cameraInsideField = originF > 0.0;
    }
    uint prevTeam = bestTeam;
    float tPrev = t0;
    float fPrev = F;
    vec3 accumColor = vec3(0.0);
    float accumAlpha = 0.0;
    int numShaded = 0;
    for (int i = 1; i <= steps && numShaded < 2; ++i)
    {
        const float t = t0 + dt * float(i);
        forceSampleField(rayOrigin + rayDir * t, iso, bestTeam, bestPhi, secondPhi, F);
        const bool entryCrossing = F > 0.0;
        if (entryCrossing != (fPrev > 0.0))
        {
            // The bubble's team is the INSIDE end's strongest team; bisect on [tPrev, t] holding it
            // fixed. 6 refinements: neighbouring pixels can be shaded by DIFFERENT proxies whose
            // march intervals differ — the residual hit error must stay below the normal's finite-
            // difference step or the fresnel visibly steps at the ownership boundary of merged shells.
            const uint hitTeam = entryCrossing ? bestTeam : prevTeam;
            float lo = tPrev, hi = t;
            for (int b = 0; b < 6; ++b)
            {
                const float mid = (lo + hi) * 0.5;
                const float fm = forceSurfaceForTeam(rayOrigin + rayDir * mid, hitTeam, iso);
                if ((fm > 0.0) == entryCrossing) hi = mid; else lo = mid;
            }
            const float tHit = (lo + hi) * 0.5;
            // Ownership: only the crossing's dominant proxy shades it; unowned crossings still
            // advance the march (their owner's own fragment draws them).
            if (forceDominantEmitter(rayOrigin + rayDir * tHit, hitTeam) == v_emitterIdx)
            {
                const vec4 layer = forceShadeHit(rayOrigin, rayDir, tHit, hitTeam, cameraInsideField, sceneDist);
                accumColor += (1.0 - accumAlpha) * layer.rgb;
                accumAlpha += (1.0 - accumAlpha) * layer.a;
                ++numShaded;
            }
        }
        prevTeam = bestTeam;
        tPrev = t;
        fPrev = F;
    }
    if (accumAlpha <= 0.002 && dot(accumColor, accumColor) < 1e-6)
        discard;

    out_color = vec4(accumColor, accumAlpha);
}
