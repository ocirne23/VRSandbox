export module RendererVK:Device;

import Core;
import :VK;

export class Device final
{
public:
    Device();
    ~Device();
    Device(const Device&) = delete;

    bool initialize();
    void destroy();

    uint32 findMemoryType(uint32 typeBits, vk::MemoryPropertyFlags requirementsMask) const;

    vk::PhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    vk::Device getDevice() const { return m_device; }
    vk::CommandPool getCommandPool() const { return m_commandPool; }
    vk::DescriptorPool getDescriptorPool() const { return m_descriptorPool; }
    uint32 getGraphicsQueueIndex() const { return m_graphicsQueueIndex; }
    vk::Queue getGraphicsQueue() const { return m_graphicsQueue; }
    bool supportsExtensions(std::vector<const char*> extensions);
    vk::DeviceSize getNonCoherentAtomSize() const { return m_nonCoherentAtomSize; }

private:

    vk::PhysicalDevice m_physicalDevice;
    vk::Device m_device;
    vk::CommandPool m_commandPool;
    vk::DescriptorPool m_descriptorPool;
    uint32 m_graphicsQueueIndex;
    vk::Queue m_graphicsQueue;
    vk::DeviceSize m_nonCoherentAtomSize;

    std::vector<vk::Format> m_supported2DOptimalFormats;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU2")
    Device device;
#pragma warning(default: 4075)
}