export module RendererVK.VK;

#pragma warning(push)
#pragma warning(disable : 5244) // '#include <>' in the purview of module appears erroneous.

export import <vulkan/vulkan.hpp>;

#pragma warning(pop)

#undef VK_NULL_HANDLE
export nullptr_t VK_NULL_HANDLE = nullptr;

#undef VK_KHR_SWAPCHAIN_EXTENSION_NAME
export const char* VK_KHR_SWAPCHAIN_EXTENSION_NAME = "VK_KHR_swapchain";

#undef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
export const char* VK_EXT_DEBUG_REPORT_EXTENSION_NAME = "VK_EXT_debug_report";

#undef VK_EXT_DEBUG_UTILS_EXTENSION_NAME
export const char* VK_EXT_DEBUG_UTILS_EXTENSION_NAME = "VK_EXT_debug_utils";

#undef VK_MAKE_API_VERSION
export constexpr uint32_t VK_MAKE_API_VERSION(uint32_t variant, uint32_t major, uint32_t minor, uint32_t patch)
{
	return ((((uint32_t)(variant)) << 29U) | (((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)));
}

#undef VK_MAKE_VERSION
export constexpr uint32_t VK_MAKE_VERSION(uint32_t major, uint32_t minor, uint32_t patch)
{
	return ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)));
}

