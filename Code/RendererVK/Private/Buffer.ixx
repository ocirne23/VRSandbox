export module RendererVK.Buffer;

import Core;
import RendererVK.VK;

export class Buffer final
{
public:
	Buffer();
	~Buffer();
	Buffer(Buffer&& move)
	{
		m_size = move.m_size;
		m_buffer = move.getBuffer();
		m_memory = move.getMemory();
		move.m_size = 0;
		move.m_buffer = nullptr;
		move.m_memory = nullptr;
	}
	Buffer(const Buffer&) = delete;

	bool initialize(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties);
	void destroy();

	void mapMemory(void* pData, vk::DeviceSize size);
	std::span<uint8> mapMemory();
	void unmapMemory();
	vk::Buffer getBuffer() const { return m_buffer; }
	vk::DeviceMemory getMemory() const { return m_memory; }

private:

	vk::DeviceSize m_size = 0;
	vk::Buffer m_buffer;
	vk::DeviceMemory m_memory;
};