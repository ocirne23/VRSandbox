export module RendererVK.Device;

import Core;
import RendererVK.VK;

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

export namespace VK
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU2")
	Device g_dev;
#pragma warning(default: 4075)
}