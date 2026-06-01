#ifndef SHARED_CONSTANTS_INC_GLSL
#define SHARED_CONSTANTS_INC_GLSL

#define ALPHA_MODE_OPAQUE 0u
#define ALPHA_MODE_MASK 1u
#define ALPHA_MODE_BLEND 2u

#define MAX_LARGE_LIGHTS_PER_GRID 6 // Must be even
#define MAX_LIGHTCELL_LIGHTS 12     // Must be even
#define GRID_SIZE 32

const uint EMPTY_ENTRY        = 0xFFFFFFFFu;
const uint INITIALIZING_ENTRY = 0xEFFFFFFFu;

#endif
