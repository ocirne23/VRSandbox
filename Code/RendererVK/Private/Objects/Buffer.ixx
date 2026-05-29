export module RendererVK:Buffer;

import Core;
import :VK;

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

    // usage2 supplies VkBufferUsageFlags2 bits (e.g. ePreprocessBufferEXT) that have no 32-bit
    // equivalent; when non-zero it supersedes usage. Requesting a shader-device-address usage (in
    // either parameter) allocates the memory with the device-address flag so getDeviceAddress works.
    bool initialize(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::BufferUsageFlags2 usage2 = {});
    void destroy();

    vk::DeviceAddress getDeviceAddress() const;

    std::span<uint8> mapMemory(uint64 offset = 0, uint64 size = vk::WholeSize);
    template<typename T>
    std::span<T> mapMemory(uint64 offsetBytes = 0, uint64 sizeBytes = vk::WholeSize)
    {
        std::span<uint8> spanBytes = mapMemory(offsetBytes, sizeBytes);
        return std::span<T>((T*)spanBytes.data(), spanBytes.size() / sizeof(T));
    }
    void unmapMemory();
    void flushMappedMemory(size_t size, size_t offset = 0);

    vk::Buffer getBuffer() const       { return m_buffer; }
    vk::DeviceMemory getMemory() const { return m_memory; }
	size_t getSize() const             { return (size_t)m_size; }

private:

    vk::DeviceSize m_size = 0;
    vk::Buffer m_buffer;
    vk::DeviceMemory m_memory;
};