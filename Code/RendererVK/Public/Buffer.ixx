module;

#include "VK.h"
#include <span>

export module RendererVK.Buffer;

export class Device;

export class Buffer
{
public:
	Buffer();
	~Buffer();
	Buffer(const Buffer&) = delete;

	bool initialize(const Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

	void copyBuffer(Buffer& srcBuffer, vk::DeviceSize size);

	std::span<uint8_t> mapMemory();
	void unmapMemory();

	vk::Buffer getBuffer() const { return m_buffer; }
	vk::DeviceMemory getMemory() const { return m_memory; }

private:

	vk::DeviceSize m_size = 0;
	vk::Buffer m_buffer;
	vk::DeviceMemory m_memory;
	const Device* m_device = nullptr;
};