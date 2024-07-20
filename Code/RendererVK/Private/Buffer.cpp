module RendererVK.Buffer;

import RendererVK.VK;
import RendererVK.Device;

Buffer::Buffer() {}
Buffer::~Buffer() 
{
	if (m_buffer)
		VK::g_dev.getDevice().destroyBuffer(m_buffer);
	if (m_memory)
		VK::g_dev.getDevice().freeMemory(m_memory);
}

bool Buffer::initialize(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
	vk::Device vkDevice = VK::g_dev.getDevice();
	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = vk::SharingMode::eExclusive;
	m_buffer = vkDevice.createBuffer(bufferInfo);
	if (!m_buffer)
	{
		assert(false && "Failed to create buffer");
		return false;
	}

	vk::MemoryRequirements memRequirements = vkDevice.getBufferMemoryRequirements(m_buffer);
	vk::MemoryAllocateInfo allocInfo = {};
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = VK::g_dev.findMemoryType(memRequirements.memoryTypeBits, properties);
	m_memory = vkDevice.allocateMemory(allocInfo);
	if (!m_memory)
	{
		assert(false && "Failed to allocate buffer memory");
		return false;
	}
	vkDevice.bindBufferMemory(m_buffer, m_memory, 0);
	return true;
}

std::span<uint8> Buffer::mapMemory()
{
	return std::span<uint8>((uint8*)VK::g_dev.getDevice().mapMemory(m_memory, 0, vk::WholeSize), (size_t)m_size);
}

void Buffer::unmapMemory()
{
	VK::g_dev.getDevice().unmapMemory(m_memory);
}