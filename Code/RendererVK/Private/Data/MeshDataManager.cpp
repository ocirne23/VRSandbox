module RendererVK;

import Core;
import :StagingManager;
import :Buffer;
import :CommandBuffer;
import :Device;

namespace
{
    constexpr vk::BufferUsageFlags2 rtUsage()
    {
        return vk::BufferUsageFlagBits2::eShaderDeviceAddress
            | vk::BufferUsageFlagBits2::eAccelerationStructureBuildInputReadOnlyKHR
            | vk::BufferUsageFlagBits2::eStorageBuffer;
    }
    constexpr vk::BufferUsageFlags2 vertexBufferUsage()
    {
        return vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eTransferSrc | vk::BufferUsageFlagBits2::eVertexBuffer | rtUsage();
    }
    constexpr vk::BufferUsageFlags2 indexBufferUsage()
    {
        return vk::BufferUsageFlagBits2::eTransferDst | vk::BufferUsageFlagBits2::eTransferSrc | vk::BufferUsageFlagBits2::eIndexBuffer | rtUsage();
    }
}

MeshDataManager::MeshDataManager()
{
}

MeshDataManager::~MeshDataManager()
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in MeshDataManager::~MeshDataManager");
    }
}

bool MeshDataManager::initialize(size_t vertexBufSize, size_t indexBufSize)
{
    m_vertexBufSize = vertexBufSize;
    m_indexBufSize = indexBufSize;

    // Extra usage beyond vertex/index: the GI ray tracer builds BLASes directly from these mega-buffers
    // (eAccelerationStructureBuildInputReadOnlyKHR + eShaderDeviceAddress) and fetches hit-triangle
    // attributes from them in the probe-trace compute shader (eStorageBuffer). eTransferSrc enables the
    // GPU copy that carries contents over on capacity growth.
    m_vertexBuffer.initialize(vertexBufSize, vertexBufferUsage(), vk::MemoryPropertyFlagBits::eDeviceLocal, false, "MeshVertex");
    m_indexBuffer.initialize(indexBufSize, indexBufferUsage(), vk::MemoryPropertyFlagBits::eDeviceLocal, false, "MeshIndex");

    return true;
}

void MeshDataManager::growBuffer(Buffer& buffer, size_t& bufSize, size_t usedSize, size_t neededSize, vk::BufferUsageFlags2 usage)
{
    size_t newSize = bufSize;
    while (newSize < neededSize)
        newSize *= 2;

    // Queued staging copies may target the buffer being replaced; submit them, then drain the GPU so
    // neither the old buffer nor its readers are in flight.
    Globals::stagingManager.flushPending();
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    assert(waitResult == vk::Result::eSuccess && "Failed to wait for device idle in MeshDataManager::growBuffer");

    Buffer oldBuffer = std::move(buffer);
    buffer.initialize(newSize, usage, vk::MemoryPropertyFlagBits::eDeviceLocal, false, oldBuffer.getDebugName());
    if (usedSize > 0)
    {
        CommandBuffer copyCommandBuffer;
        copyCommandBuffer.initialize(vk::CommandBufferLevel::ePrimary);
        vk::CommandBuffer vkCmd = copyCommandBuffer.begin(true);
        const vk::BufferCopy region{ .srcOffset = 0, .dstOffset = 0, .size = usedSize };
        vkCmd.copyBuffer(oldBuffer.getBuffer(), buffer.getBuffer(), 1, &region);
        copyCommandBuffer.end();
        copyCommandBuffer.submitGraphics();
        auto copyWaitResult = Globals::device.getGraphicsQueue().waitIdle();
        assert(copyWaitResult == vk::Result::eSuccess && "Failed to wait for grow copy in MeshDataManager::growBuffer");
    }
    bufSize = newSize;
    m_generation++;
    printf("MeshDataManager: grew buffer to %zu bytes\n", newSize);
}

size_t MeshDataManager::uploadVertexData(const void* pData, size_t size)
{
    if (m_vertexBufOffset + size > m_vertexBufSize)
        growBuffer(m_vertexBuffer, m_vertexBufSize, m_vertexBufOffset, m_vertexBufOffset + size, vertexBufferUsage());
    const size_t offset = m_vertexBufOffset;
    Globals::stagingManager.upload(m_vertexBuffer.getBuffer(), size, pData, m_vertexBufOffset);
    m_vertexBufOffset += size;
    return offset;
}

size_t MeshDataManager::uploadIndexData(const void* pData, size_t size)
{
    if (m_indexBufOffset + size > m_indexBufSize)
        growBuffer(m_indexBuffer, m_indexBufSize, m_indexBufOffset, m_indexBufOffset + size, indexBufferUsage());
    const size_t offset = m_indexBufOffset;
    Globals::stagingManager.upload(m_indexBuffer.getBuffer(), size, pData, m_indexBufOffset);
    m_indexBufOffset += size;
    return offset;
}