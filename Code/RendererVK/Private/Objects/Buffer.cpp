module RendererVK:Buffer;

import :VK;
import :Device;

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

bool Buffer::initialize(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties)
{
    if (m_buffer)
        destroy();

    m_size = size;
    vk::Device vkDevice = Globals::device.getDevice();
    vk::BufferCreateInfo bufferInfo = {};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;
    vk::ResultValue<vk::Buffer> bufResult = vkDevice.createBuffer(bufferInfo);
    if (bufResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create buffer");
        return false;
    }
    m_buffer = bufResult.value;

    vk::MemoryRequirements memRequirements = vkDevice.getBufferMemoryRequirements(m_buffer);
    vk::MemoryAllocateInfo allocInfo = {};
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
    return true;
}

std::span<uint8> Buffer::mapMemory()
{
    vk::ResultValue<void*> result = Globals::device.getDevice().mapMemory(m_memory, 0, vk::WholeSize);
    if (result.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to map buffer memory");
        return {};
    }
    return std::span<uint8>((uint8*)result.value, (size_t)m_size);

}

void Buffer::unmapMemory()
{
    Globals::device.getDevice().unmapMemory(m_memory);
}