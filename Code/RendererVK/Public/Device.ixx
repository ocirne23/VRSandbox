export module RendererVK.Device;

import Core;
import RendererVK.VK;
import RendererVK.Instance;

export class Device final
{
public:
	Device();
	~Device();
	Device(const Device&) = delete;

	bool initialize(const Instance& instance);

	uint32 findMemoryType(uint32 typeBits, vk::MemoryPropertyFlags requirementsMask) const;

	vk::PhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
	vk::Device getDevice() const { return m_device; }
	vk::CommandPool getCommandPool() const { return m_commandPool; }
	uint32 getGraphicsQueueIndex() const { return m_graphicsQueueIndex; }
	vk::Queue getGraphicsQueue() const { return m_graphicsQueue; }
	bool supportsExtensions(std::vector<const char*> extensions);

private:

	vk::PhysicalDevice m_physicalDevice;
	vk::Device m_device;
	vk::CommandPool m_commandPool;
	uint32 m_graphicsQueueIndex;
	vk::Queue m_graphicsQueue;
};