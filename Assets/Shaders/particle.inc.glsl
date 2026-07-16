#ifndef PARTICLE_INC_GLSL
#define PARTICLE_INC_GLSL

// GPU particle system shared declarations (ParticlePipeline). MAX_PARTICLES, MAX_PARTICLE_EMITTERS,
// PARTICLE_SIM_GROUP_SIZE, PARTICLE_FLAG_* and PARTICLE_TEX_NONE are injected by the engine from
// RendererVKLayout (Layout.ixx). Structs must stay in sync with Layout.ixx.

// One live particle in the persistent pool (48 bytes).
struct Particle
{
    vec4 posAge;   // xyz = world position, w = age (s)
    vec4 velLife;  // xyz = velocity (m/s), w = lifetime (s)
    uvec4 misc;    // x = emitter slot, y = RNG seed, z = rotation (float bits), w = spin rad/s (float bits)
};

// Mirror of RendererVKLayout::ParticleEmitterGpu (192 bytes).
struct ParticleEmitter
{
    vec4 posSpawnRadius;  // xyz = world position, w = spawn radius (m)
    vec4 rotation;        // quat; spawn cone axis = local +Y
    vec4 velocityInherit; // xyz = emitter velocity (m/s), w = inherit factor
    vec4 spawnParams;     // x = cone angle (rad), y = speed min, z = speed max, w = spawn on shell
    vec4 lifeParams;      // x = life min (s), y = life max, z = gravity (m/s^2, -Y), w = drag (1/s)
    vec4 noiseParams;     // x = turbulence accel (m/s^2), y = frequency (1/m), z = scroll (m/s), w = bounce
    vec4 sizeParams;      // x = size start (m), y = size end, z = size variance, w = velocity stretch (s)
    vec4 colorStart;      // rgb = linear color * intensity, a = start alpha
    vec4 colorEnd;
    vec4 fadeParams;      // x = fade-in end (life frac), y = fade-out start, z = additivity, w = soft fade dist (m)
    vec4 spinParams;      // x = max spin (rad/s), y = random initial rotation (0/1), z = lit emissive floor, w unused
    uvec4 texFlags;       // x = texture idx, y = PARTICLE_FLAG_* bits, z = flipbook cols | rows << 16, w = flipbook fps (float bits)
};

// GPU counters block: sim dispatch args + per-parity draw args (instanceCount IS the alive count) +
// the dead-stack top. Bound as one buffer that is also the indirect dispatch/draw source.
// c_draw[parity * 4 + 0..3] = vertexCount(6), instanceCount, firstVertex(0), firstInstance(0).
#define PARTICLE_COUNTERS_BLOCK \
    uvec4 c_simGroups;          \
    uint  c_draw[8];            \
    int   c_deadCount;          \
    uint  c_pad0; uint c_pad1; uint c_pad2;

// ---- RNG (pcg) ----
uint particlePcg(uint v)
{
    v = v * 747796405u + 2891336453u;
    uint w = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    return (w >> 22u) ^ w;
}
float particleRand(inout uint state) // [0,1)
{
    state = particlePcg(state);
    return float(state) * (1.0 / 4294967296.0);
}

vec3 particleQuatRotate(vec4 q, vec3 v)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

// ---- cheap 3D value noise (turbulence) ----
float particleHash1(uvec3 v)
{
    uint h = v.x * 1597334673u + v.y * 3812015801u + v.z * 2798796415u;
    h = particlePcg(h);
    return float(h) * (1.0 / 4294967296.0);
}
float particleNoise(vec3 p)
{
    vec3 i = floor(p);
    vec3 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    uvec3 b = uvec3(ivec3(i) + 0x8000);
    float n000 = particleHash1(b + uvec3(0u, 0u, 0u)), n100 = particleHash1(b + uvec3(1u, 0u, 0u));
    float n010 = particleHash1(b + uvec3(0u, 1u, 0u)), n110 = particleHash1(b + uvec3(1u, 1u, 0u));
    float n001 = particleHash1(b + uvec3(0u, 0u, 1u)), n101 = particleHash1(b + uvec3(1u, 0u, 1u));
    float n011 = particleHash1(b + uvec3(0u, 1u, 1u)), n111 = particleHash1(b + uvec3(1u, 1u, 1u));
    float nx00 = mix(n000, n100, f.x), nx10 = mix(n010, n110, f.x);
    float nx01 = mix(n001, n101, f.x), nx11 = mix(n011, n111, f.x);
    return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}
// Pseudo-turbulence acceleration: three decorrelated value noises in [-1,1] (not divergence-free, but
// visually adequate for smoke/ember wander at a fraction of a real curl evaluation's cost).
vec3 particleTurbulence(vec3 p)
{
    return vec3(particleNoise(p) * 2.0 - 1.0,
                particleNoise(p + vec3(31.416, 27.183, 12.793)) * 2.0 - 1.0,
                particleNoise(p + vec3(-17.321, 41.421, -23.606)) * 2.0 - 1.0);
}

#endif
