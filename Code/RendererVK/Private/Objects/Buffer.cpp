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

bool Buffer::initialize(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::BufferUsageFlags2 usage2, bool useBackingStore)
{
    if (m_buffer)
        destroy();

    m_size = size;
    m_usage = usage;
    m_properties = properties;
    m_usage2 = usage2;
    m_hasBackingStore = useBackingStore;
    vk::Device vkDevice = Globals::device.getDevice();
    vk::BufferUsageFlags2CreateInfo usage2Info{ .usage = usage2 };
    vk::BufferCreateInfo bufferInfo = {};
    bufferInfo.size = size;
    if (usage2)
    {
        // When flags2 are used the legacy usage field must be 0; the caller passes all bits in usage2.
        bufferInfo.pNext = &usage2Info;
    }
    else
    {
        bufferInfo.usage = usage;
    }
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    vk::ResultValue<vk::Buffer> bufResult = vkDevice.createBuffer(bufferInfo);
    if (bufResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create buffer");
        return false;
    }
    m_buffer = bufResult.value;

    const bool needsDeviceAddress = (bool)(usage & vk::BufferUsageFlagBits::eShaderDeviceAddress)
        || (bool)(usage2 & vk::BufferUsageFlagBits2::eShaderDeviceAddress);

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
    return initialize(newSize, m_usage, m_properties, m_usage2, true);
}

bool Buffer::uploadBackingStore()
{
    assert(m_hasBackingStore && "uploadBackingStore() requires a buffer initialized with useBackingStore = true");
    if (!m_hasBackingStore || m_backingStore.empty())
        return true;
    const size_t uploadSize = std::min((size_t)m_size, m_backingStore.size());
    if (m_properties & vk::MemoryPropertyFlagBits::eHostVisible)
    {
        std::span<uint8> mapped = mapMemory(0, uploadSize);
        if (mapped.empty())
            return false;
        memcpy(mapped.data(), m_backingStore.data(), uploadSize);
        flushMappedMemory(uploadSize);
        unmapMemory();
    }
    else
    {
        Globals::stagingManager.upload(m_buffer, uploadSize, m_backingStore.data());
    }
    return true;
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