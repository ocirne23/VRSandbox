#pragma once

#include <cassert>
#include <cstdlib>
#include "InitSeg.h" // OC_INIT_SEG + the engine-wide static-init ordering table

#ifdef NDEBUG
    #undef assert
    #define assert(expression) ((void)sizeof(expression))
#endif