module;

#include "VK.h"

module RendererVK.Buffer;

import RendererVK.Device;
import RendererVK.CommandBuffer;

Buffer::Buffer() {}
Buffer::~Buffer() 
{
	if (m_buffer)
		m_device->getDevice().destroyBuffer(m_buffer);
	if (m_memory)
		m_device->getDevice().freeMemory(m_memory);
}

bool Buffer::initialize(const Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
	m_device = &device;
	vk::Device vkDevice = device.getDevice();
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
	allocInfo.memoryTypeIndex = device.findMemoryType(memRequirements.memoryTypeBits, properties);
	m_memory = vkDevice.allocateMemory(allocInfo);
	if (!m_memory)
	{
		assert(false && "Failed to allocate buffer memory");
		return false;
	}
	vkDevice.bindBufferMemory(m_buffer, m_memory, 0);
	return true;
}

void Buffer::copyBuffer(Buffer& buffer, vk::DeviceSize size)
{
	/*
	CommandBuffer commandBuffer;
	vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
	vk::BufferCopy copyRegion = { .size = size };

	vkCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	vkCommandBuffer.copyBuffer(buffer.getBuffer(), m_buffer, 1, &copyRegion);
	vkCommandBuffer.end();

	vk::SubmitInfo submitInfo = {};
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vkCommandBuffer;

	m_device->getGraphicsQueue().submit(1, &submitInfo, nullptr);
	m_device->getGraphicsQueue().waitIdle();*/
}

std::span<uint8_t> Buffer::mapMemory()
{
	return std::span<uint8_t>((uint8_t*)m_device->getDevice().mapMemory(m_memory, 0, VK_WHOLE_SIZE), (size_t)m_size);
}

void Buffer::unmapMemory()
{
	m_device->getDevice().unmapMemory(m_memory);
}