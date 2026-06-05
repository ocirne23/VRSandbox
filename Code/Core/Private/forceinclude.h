#pragma once

#include <cassert>
#include <cstdlib>

#ifdef NDEBUG
    #undef assert
    #define assert(expression) ((void)sizeof(expression))
#endif