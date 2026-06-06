#ifndef SHARED_CONSTANTS_INC_GLSL
#define SHARED_CONSTANTS_INC_GLSL

#define ALPHA_MODE_OPAQUE 0u
#define ALPHA_MODE_MASK 1u
#define ALPHA_MODE_BLEND 2u

#define MAX_LARGE_LIGHTS_PER_GRID 6 // Must be even
#define MAX_LIGHTCELL_LIGHTS 16     // Must be even
#define GRID_SIZE 32
#define NUM_SHADOW_CASCADES 6       // Must match RendererVKLayout::NUM_SHADOW_CASCADES

const uint EMPTY_ENTRY        = 0xFFFFFFFFu;
const uint INITIALIZING_ENTRY = 0xEFFFFFFFu;

uint getPositionHash(ivec3 p) 
{
    uvec3 q = uvec3(p);
    q = q * uvec3(1597334673u, 3812015801u, 2798796415u);
    uint n = q.x ^ q.y ^ q.z;
	// pcg hash
	n = n * 747796405u + 2891336453u;
	n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
	n = (n >> 22u) ^ n;
    return n;
}

vec3 randomColor(uint seed) 
{
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    seed *= 0x846ca68bu;
    seed ^= seed >> 16;
    vec3 bits = vec3(float(seed & 255u), float((seed >> 8) & 255u), float((seed >> 16) & 255u));
    return bits / 255.0;
}
vec3 randomColor(ivec3 seed) 
{
    uvec3 u = uvec3(seed);
    uint hash = u.x * 1597334673u + u.y * 3812015801u + u.z * 2798796415u;
	return randomColor(hash);
}
vec3 cascadeDebugColor(int cascade)
{
	if (cascade == 0) return vec3(1.0, 0.0, 0.0);
	if (cascade == 1) return vec3(0.0, 1.0, 0.0);
	if (cascade == 2) return vec3(0.0, 0.0, 1.0);
    if (cascade == 3) return vec3(1.0, 0.0, 1.0);
    if (cascade == 4) return vec3(0.0, 1.0, 1.0);
	return vec3(1.0, 1.0, 0.0);
}

#endif
