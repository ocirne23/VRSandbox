module;

#include <vulkan/vulkan_core.h> // for vkCmdPushDescriptorSetKHR

export module RendererVK:VK;

export import vulkan_hpp;
import Core;

#undef VK_NULL_HANDLE
export nullptr_t VK_NULL_HANDLE = nullptr;

#undef VK_QUEUE_FAMILY_IGNORED
export constexpr uint32 VK_QUEUE_FAMILY_IGNORED = (~0u);
#undef VK_WHOLE_SIZE
export constexpr uint64 VK_WHOLE_SIZE = (~0ULL);

export const char* VK_VALIDATION_LAYER_NAME = "VK_LAYER_KHRONOS_validation";

#undef VK_KHR_SWAPCHAIN_EXTENSION_NAME
export const char* VK_KHR_SWAPCHAIN_EXTENSION_NAME = "VK_KHR_swapchain";

#undef VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
export const char* VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME = "VK_KHR_push_descriptor";

#undef VK_KHR_MAINTENANCE_5_EXTENSION_NAME
export const char* VK_KHR_MAINTENANCE_5_EXTENSION_NAME = "VK_KHR_maintenance5";

#undef VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME
export const char* VK_EXT_DEVICE_GENERATED_COMMANDS_EXTENSION_NAME = "VK_EXT_device_generated_commands";

#undef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
export const char* VK_EXT_DEBUG_REPORT_EXTENSION_NAME = "VK_EXT_debug_report";

#undef VK_EXT_DEBUG_UTILS_EXTENSION_NAME
export const char* VK_EXT_DEBUG_UTILS_EXTENSION_NAME = "VK_EXT_debug_utils";

#undef VK_MAKE_API_VERSION
export constexpr uint32 VK_MAKE_API_VERSION(uint32 variant, uint32 major, uint32 minor, uint32 patch)
{
    return ((((uint32)(variant)) << 29U) | (((uint32)(major)) << 22U) | (((uint32)(minor)) << 12U) | ((uint32)(patch)));
}

#undef VK_MAKE_VERSION
export constexpr uint32 VK_MAKE_VERSION(uint32 major, uint32 minor, uint32 patch)
{
    return ((((uint32)(major)) << 22U) | (((uint32)(minor)) << 12U) | ((uint32)(patch)));
}

export PFN_vkCmdPushDescriptorSetKHR pfVkCmdPushDescriptorSetKHR = nullptr;
export extern "C" void vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
{
    pfVkCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}

// VK_EXT_device_generated_commands entrypoints.
// vulkan-1.lib's static loader does not export these, so we forward the global C symbols
// (used by vulkan.hpp's static dispatcher) to function pointers loaded in Device::initialize.
export PFN_vkGetGeneratedCommandsMemoryRequirementsEXT pfVkGetGeneratedCommandsMemoryRequirementsEXT = nullptr;
export extern "C" void vkGetGeneratedCommandsMemoryRequirementsEXT(VkDevice device, const VkGeneratedCommandsMemoryRequirementsInfoEXT* pInfo, VkMemoryRequirements2* pMemoryRequirements)
{
    pfVkGetGeneratedCommandsMemoryRequirementsEXT(device, pInfo, pMemoryRequirements);
}

export PFN_vkCmdPreprocessGeneratedCommandsEXT pfVkCmdPreprocessGeneratedCommandsEXT = nullptr;
export extern "C" void vkCmdPreprocessGeneratedCommandsEXT(VkCommandBuffer commandBuffer, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo, VkCommandBuffer stateCommandBuffer)
{
    pfVkCmdPreprocessGeneratedCommandsEXT(commandBuffer, pGeneratedCommandsInfo, stateCommandBuffer);
}

export PFN_vkCmdExecuteGeneratedCommandsEXT pfVkCmdExecuteGeneratedCommandsEXT = nullptr;
export extern "C" void vkCmdExecuteGeneratedCommandsEXT(VkCommandBuffer commandBuffer, VkBool32 isPreprocessed, const VkGeneratedCommandsInfoEXT* pGeneratedCommandsInfo)
{
    pfVkCmdExecuteGeneratedCommandsEXT(commandBuffer, isPreprocessed, pGeneratedCommandsInfo);
}

export PFN_vkCreateIndirectCommandsLayoutEXT pfVkCreateIndirectCommandsLayoutEXT = nullptr;
export extern "C" VkResult vkCreateIndirectCommandsLayoutEXT(VkDevice device, const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectCommandsLayoutEXT* pIndirectCommandsLayout)
{
    return pfVkCreateIndirectCommandsLayoutEXT(device, pCreateInfo, pAllocator, pIndirectCommandsLayout);
}

export PFN_vkDestroyIndirectCommandsLayoutEXT pfVkDestroyIndirectCommandsLayoutEXT = nullptr;
export extern "C" void vkDestroyIndirectCommandsLayoutEXT(VkDevice device, VkIndirectCommandsLayoutEXT indirectCommandsLayout, const VkAllocationCallbacks* pAllocator)
{
    pfVkDestroyIndirectCommandsLayoutEXT(device, indirectCommandsLayout, pAllocator);
}

export PFN_vkCreateIndirectExecutionSetEXT pfVkCreateIndirectExecutionSetEXT = nullptr;
export extern "C" VkResult vkCreateIndirectExecutionSetEXT(VkDevice device, const VkIndirectExecutionSetCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkIndirectExecutionSetEXT* pIndirectExecutionSet)
{
    return pfVkCreateIndirectExecutionSetEXT(device, pCreateInfo, pAllocator, pIndirectExecutionSet);
}

export PFN_vkDestroyIndirectExecutionSetEXT pfVkDestroyIndirectExecutionSetEXT = nullptr;
export extern "C" void vkDestroyIndirectExecutionSetEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, const VkAllocationCallbacks* pAllocator)
{
    pfVkDestroyIndirectExecutionSetEXT(device, indirectExecutionSet, pAllocator);
}

export PFN_vkUpdateIndirectExecutionSetPipelineEXT pfVkUpdateIndirectExecutionSetPipelineEXT = nullptr;
export extern "C" void vkUpdateIndirectExecutionSetPipelineEXT(VkDevice device, VkIndirectExecutionSetEXT indirectExecutionSet, uint32_t executionSetWriteCount, const VkWriteIndirectExecutionSetPipelineEXT* pExecutionSetWrites)
{
    pfVkUpdateIndirectExecutionSetPipelineEXT(device, indirectExecutionSet, executionSetWriteCount, pExecutionSetWrites);
}