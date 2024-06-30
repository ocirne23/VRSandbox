export module RendererVK.VK;

export import <vulkan/vulkan.hpp>;

#undef VK_NULL_HANDLE
export nullptr_t VK_NULL_HANDLE = nullptr;

#undef VK_KHR_SWAPCHAIN_EXTENSION_NAME
export const char* VK_KHR_SWAPCHAIN_EXTENSION_NAME = "VK_KHR_swapchain";

#undef VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
export const char* VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME = "VK_KHR_push_descriptor";

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

export PFN_vkCmdPushDescriptorSetKHR pfVkCmdPushDescriptorSetKHR = nullptr;
export void vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
{
	pfVkCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}
