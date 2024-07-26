export module RendererVK.VK;

export import <vulkan/vulkan.hpp>;

import Core;

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
export void vkCmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t set, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites)
{
    pfVkCmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}

/*
static std::atomic<size_t> g_vkAllocatedSize = 0;

vk::AllocationCallbacks setupAllocationCallbacks()
{
    vk::AllocationCallbacks allocationCallbacks;
    allocationCallbacks.pfnAllocation =
        [](void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope) -> void*
        {
            //g_vkAllocatedSize += size;
            return _aligned_malloc(size, alignment);
        };
    allocationCallbacks.pfnReallocation =
        [](void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope) -> void*
        {
            //g_vkAllocatedSize += size;
            return _aligned_realloc(pOriginal, size, alignment);
        };

    allocationCallbacks.pfnFree =
        [](void* pUserData, void* pMemory) -> void
        {
            return _aligned_free(pMemory);
        };
    allocationCallbacks.pfnInternalAllocation =
        [](void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope) -> void
        {
            g_vkAllocatedSize += size;
        };
    allocationCallbacks.pfnInternalFree =
        [](void* pUserData, size_t size, VkInternalAllocationType allocationType, VkSystemAllocationScope allocationScope) -> void
        {
            g_vkAllocatedSize -= size;
        };
    return allocationCallbacks;
}*/

export namespace VK
{
    size_t getAllocatedSize()
    {
        return 0;//g_vkAllocatedSize.load();
    }

    vk::AllocationCallbacks* getAllocationCallbacks()
    {
        //static vk::AllocationCallbacks s_allocationCallbacks = setupAllocationCallbacks();
        //return &s_allocationCallbacks;
        return nullptr;
    }
}

