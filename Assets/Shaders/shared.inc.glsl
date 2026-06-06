#ifndef SHARED_CONSTANTS_INC_GLSL
#define SHARED_CONSTANTS_INC_GLSL

#define ALPHA_MODE_OPAQUE 0u
#define ALPHA_MODE_MASK 1u
#define ALPHA_MODE_BLEND 2u

#define NUM_SHADOW_CASCADES 6       // Must match RendererVKLayout::NUM_SHADOW_CASCADES

const uint EMPTY_ENTRY        = 0xFFFFFFFFu;
const uint INITIALIZING_ENTRY = 0xEFFFFFFFu;

const float PI = 3.14159265359;

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
