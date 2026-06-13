module RendererVK:Buffer;

import :VK;
import :Device;
import :StagingManager;

Buffer::Buffer() {}
Buffer::~Buffer()
{
    destroy();
}

void Buffer::destroy()
{
    if (m_buffer)
    {
        Globals::device.getDevice().destroyBuffer(m_buffer);
        m_buffer = VK_NULL_HANDLE;
    }
    if (m_memory)
    {
        Globals::device.getDevice().freeMemory(m_memory);
        m_memory = VK_NULL_HANDLE;
    }
}

bool Buffer::initialize(vk::DeviceSize size, vk::BufferUsageFlags2 usage, vk::MemoryPropertyFlags properties, bool useBackingStore)
{
    if (m_buffer)
        destroy();

    m_size = size;
    m_usage = usage;
    m_properties = properties;
    m_hasBackingStore = useBackingStore;
    m_uploadOffset = 0;
    vk::Device vkDevice = Globals::device.getDevice();
    vk::BufferUsageFlags2CreateInfo usageInfo{ .usage = usage };
    vk::BufferCreateInfo bufferInfo = {};
    bufferInfo.pNext = &usageInfo;
    bufferInfo.size = size;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    vk::ResultValue<vk::Buffer> bufResult = vkDevice.createBuffer(bufferInfo);
    if (bufResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create buffer");
        return false;
    }
    m_buffer = bufResult.value;

    const bool needsDeviceAddress = (bool)(usage & vk::BufferUsageFlagBits2::eShaderDeviceAddress);

    vk::MemoryRequirements memRequirements = vkDevice.getBufferMemoryRequirements(m_buffer);
    vk::MemoryAllocateFlagsInfo allocFlagsInfo{ .flags = vk::MemoryAllocateFlagBits::eDeviceAddress };
    vk::MemoryAllocateInfo allocInfo = {};
    if (needsDeviceAddress)
        allocInfo.pNext = &allocFlagsInfo;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = Globals::device.findMemoryType(memRequirements.memoryTypeBits, properties);
    vk::ResultValue<vk::DeviceMemory> memResult = vkDevice.allocateMemory(allocInfo);
    if (memResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to allocate buffer memory");
        return false;
    }
    m_memory = memResult.value;
    auto bindResult = vkDevice.bindBufferMemory(m_buffer, m_memory, 0);
    if (bindResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to bind buffer memory");
        return false;
    }

    if (m_hasBackingStore)
        return uploadBackingStore();
    return true;
}

bool Buffer::resize(vk::DeviceSize newSize)
{
    assert(m_hasBackingStore && "resize() requires a buffer initialized with useBackingStore = true");
    if (!m_hasBackingStore)
        return false;
    const vk::DeviceSize uploadOffset = m_uploadOffset;
    const bool result = initialize(newSize, m_usage, m_properties, true);
    m_uploadOffset = uploadOffset;
    return result;
}

bool Buffer::upload(vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset)
{
    if (dataSize == 0)
        return true;
    assert(dstOffset + dataSize <= m_size);
    if (m_properties & vk::MemoryPropertyFlagBits::eHostVisible)
    {
        std::span<uint8> mapped = mapMemory(dstOffset, dataSize);
        if (mapped.empty())
            return false;
        memcpy(mapped.data(), data, (size_t)dataSize);
        flushMappedMemory((size_t)dataSize, (size_t)dstOffset);
        unmapMemory();
    }
    else
    {
        Globals::stagingManager.upload(m_buffer, dataSize, data, dstOffset);
    }
    return true;
}

vk::DeviceSize Buffer::appendUpload(vk::DeviceSize dataSize, const void* data)
{
    const vk::DeviceSize offset = m_uploadOffset;
    if (m_hasBackingStore)
    {
        const uint8* bytes = (const uint8*)data;
        m_backingStore.insert(m_backingStore.end(), bytes, bytes + dataSize);
    }
    upload(dataSize, data, offset);
    m_uploadOffset += dataSize;
    return offset;
}

bool Buffer::uploadBackingStore()
{
    assert(m_hasBackingStore && "uploadBackingStore() requires a buffer initialized with useBackingStore = true");
    if (!m_hasBackingStore || m_backingStore.empty())
        return true;
    return upload(std::min((size_t)m_size, m_backingStore.size()), m_backingStore.data());
}

vk::DeviceAddress Buffer::getDeviceAddress() const
{
    return Globals::device.getDevice().getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = m_buffer });
}

std::span<uint8> Buffer::mapMemory(uint64 offset, uint64 size)
{
    const static vk::DeviceSize atomSize = Globals::device.getNonCoherentAtomSize();
    vk::DeviceSize mapSize = (size == (size_t)vk::WholeSize) ? vk::WholeSize : ((size + atomSize - 1) & ~(atomSize - 1));
    if (mapSize != vk::WholeSize && mapSize > m_size - offset)
    {
        mapSize = vk::WholeSize;
    }
    vk::ResultValue<void*> result = Globals::device.getDevice().mapMemory(m_memory, offset, mapSize);
    if (result.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to map buffer memory");
        return {};
    }
    return std::span<uint8>((uint8*)result.value, (size_t)std::min(size, m_size - offset));
}

void Buffer::unmapMemory()
{
    Globals::device.getDevice().unmapMemory(m_memory);
}

void Buffer::flushMappedMemory(size_t size, size_t offset)
{
    const static vk::DeviceSize atomSize = Globals::device.getNonCoherentAtomSize();
    // vk::WholeSize must be passed through untouched: rounding it up to atomSize overflows to 0 and would
    // flush nothing. Vulkan handles WholeSize natively (flush to the end of the allocation).
    vk::DeviceSize flushSize = (size == (size_t)vk::WholeSize) ? vk::WholeSize : ((size + atomSize - 1) & ~(atomSize - 1));
	if (flushSize != vk::WholeSize && flushSize > m_size - offset)
	{
		flushSize = vk::WholeSize;
	}
    [[maybe_unused]] auto flushResult = Globals::device.getDevice().flushMappedMemoryRanges({
        vk::MappedMemoryRange{
            .memory = m_memory,
            .offset = offset,
            .size = flushSize
        }});
    assert(flushResult == vk::Result::eSuccess);
}