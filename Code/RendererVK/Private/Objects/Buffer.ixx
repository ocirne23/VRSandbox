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

    std::span<uint8> mapMemory();
    template<typename T>
    std::span<T> mapMemory()
    {
        std::span<uint8> spanBytes = mapMemory();
        return std::span<T>((T*)spanBytes.data(), spanBytes.size() / sizeof(T));
    }
    void unmapMemory();

    vk::Buffer getBuffer() const { return m_buffer; }
    vk::DeviceMemory getMemory() const { return m_memory; }

private:

    vk::DeviceSize m_size = 0;
    vk::Buffer m_buffer;
    vk::DeviceMemory m_memory;
};