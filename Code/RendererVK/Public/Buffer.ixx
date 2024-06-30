export module RendererVK.Buffer;

export class Device;

import Core;
import RendererVK.VK;

export class Buffer
{
public:
	Buffer();
	~Buffer();
	Buffer(const Buffer&) = delete;

	bool initialize(const Device& device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);

	std::span<uint8> mapMemory();
	void unmapMemory();
	vk::Buffer getBuffer() const { return m_buffer; }
	vk::DeviceMemory getMemory() const { return m_memory; }

private:

	vk::DeviceSize m_size = 0;
	vk::Buffer m_buffer;
	vk::DeviceMemory m_memory;
	const Device* m_device = nullptr;
};