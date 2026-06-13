// VulkanMemoryAllocator implementation translation unit.
//
// This is a plain (non-module) global-module source file: it compiles the VMA implementation once so
// its symbols have external linkage and can be linked by the :Allocator module impl unit (which includes
// the same header without VMA_IMPLEMENTATION). Keeping the implementation out of a module unit avoids
// giving VMA's symbols module linkage.
//
// Static Vulkan functions: the renderer links vulkan-1.lib and uses vulkan-hpp's static dispatcher, so
// VMA can call the core entry points directly rather than loading them dynamically.
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

// Print every allocation that is still alive when its memory block is destroyed (i.e. leaked at
// shutdown), including the debug name set via Allocator::createBuffer/createImage. This fires just
// before VMA's "Some allocations were not freed" assert, so the log pinpoints the leak.
#include <cstdio>
#define VMA_LEAK_LOG_FORMAT(format, ...) do { fprintf(stderr, (format), __VA_ARGS__); fprintf(stderr, "\n"); } while (false)

#pragma warning(push, 0)
#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"
#pragma warning(pop)
