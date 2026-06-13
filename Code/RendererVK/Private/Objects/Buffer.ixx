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
        m_usage = move.m_usage;
        m_properties = move.m_properties;
        m_usage2 = move.m_usage2;
        m_backingStore = std::move(move.m_backingStore);
        m_hasBackingStore = move.m_hasBackingStore;
        move.m_size = 0;
        move.m_buffer = nullptr;
        move.m_memory = nullptr;
        move.m_hasBackingStore = false;
    }
    Buffer(const Buffer&) = delete;

    // usage2 supplies VkBufferUsageFlags2 bits (e.g. ePreprocessBufferEXT) that have no 32-bit
    // equivalent; when non-zero it supersedes usage. Requesting a shader-device-address usage (in
    // either parameter) allocates the memory with the device-address flag so getDeviceAddress works.
    bool initialize(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::BufferUsageFlags2 usage2 = {}, bool useBackingStore = false);
    void destroy();

    bool resize(vk::DeviceSize newSize);

    std::vector<uint8>& getBackingStore() { return m_backingStore; }
    template<typename T>
    std::span<T> getBackingStoreAs() { return std::span<T>((T*)m_backingStore.data(), m_backingStore.size() / sizeof(T)); }
    template<typename T>
    void appendToBackingStore(std::span<const T> items)
    {
        const uint8* bytes = (const uint8*)items.data();
        m_backingStore.insert(m_backingStore.end(), bytes, bytes + items.size_bytes());
    }
    bool uploadBackingStore();

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

    vk::BufferUsageFlags m_usage;
    vk::MemoryPropertyFlags m_properties;
    vk::BufferUsageFlags2 m_usage2;
    std::vector<uint8> m_backingStore;
    bool m_hasBackingStore = false;
};