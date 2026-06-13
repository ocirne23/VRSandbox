export module RendererVK:Buffer;

import Core;
import :VK;
import :Allocator;

export class Buffer final
{
public:
    Buffer();
    ~Buffer();
    Buffer(Buffer&& move)
    {
        m_size = move.m_size;
        m_buffer = move.getBuffer();
        m_allocation = move.m_allocation;
        m_mappedData = move.m_mappedData;
        m_usage = move.m_usage;
        m_properties = move.m_properties;
        m_debugName = move.m_debugName;
        m_backingStore = std::move(move.m_backingStore);
        m_uploadOffset = move.m_uploadOffset;
        m_hasBackingStore = move.m_hasBackingStore;
        move.m_size = 0;
        move.m_buffer = nullptr;
        move.m_allocation = nullptr;
        move.m_mappedData = nullptr;
        move.m_hasBackingStore = false;
    }
    Buffer(const Buffer&) = delete;

    // Requesting a shader-device-address usage allocates the memory with the device-address flag so
    // getDeviceAddress works.
    // debugName (optional) is attached to the underlying VMA allocation to identify it in leak reports.
    bool initialize(vk::DeviceSize size, vk::BufferUsageFlags2 usage, vk::MemoryPropertyFlags properties, bool useBackingStore = false, const char* debugName = nullptr);
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
    bool upload(vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset = 0);
    template<typename T>
    bool upload(std::span<const T> items, vk::DeviceSize dstOffset = 0)
    {
        return upload(items.size_bytes(), items.data(), dstOffset);
    }

    vk::DeviceSize appendUpload(vk::DeviceSize dataSize, const void* data);
    template<typename T>
    vk::DeviceSize appendUpload(std::span<const T> items)
    {
        return appendUpload(items.size_bytes(), items.data());
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
	size_t getSize() const             { return (size_t)m_size; }

private:

    vk::DeviceSize m_size = 0;
    vk::Buffer m_buffer;
    VmaAllocation m_allocation = nullptr;
    void* m_mappedData = nullptr;

    vk::BufferUsageFlags2 m_usage;
    vk::MemoryPropertyFlags m_properties;
    const char* m_debugName = nullptr;
    std::vector<uint8> m_backingStore;
    vk::DeviceSize m_uploadOffset = 0;
    bool m_hasBackingStore = false;
};