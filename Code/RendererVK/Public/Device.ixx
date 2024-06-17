export module RendererVK.Device;

import RendererVK.VK;
import RendererVK.Instance;

export class Device final
{
public:
	Device();
	~Device();
	Device(const Device&) = delete;

	bool initialize(const Instance& instance);

	uint32_t findMemoryType(uint32_t typeBits, vk::MemoryPropertyFlags requirementsMask) const;

	vk::PhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
	vk::Device getDevice() const { return m_device; }
	vk::CommandPool getCommandPool() const { return m_commandPool; }
	uint32_t getGraphicsQueueIndex() const { return m_graphicsQueueIndex; }
	vk::Queue getGraphicsQueue() const { return m_graphicsQueue; }
	vk::DescriptorPool getDescriptorPool() const { return m_descriptorPool; }
	bool supportsExtensions(std::vector<const char*> extensions);

private:

	vk::PhysicalDevice m_physicalDevice;
	vk::Device m_device;
	vk::CommandPool m_commandPool;
	uint32_t m_graphicsQueueIndex;
	vk::Queue m_graphicsQueue;
	vk::DescriptorPool m_descriptorPool;
};