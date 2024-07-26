module RendererVK.MeshDataManager;

import Core;
import RendererVK.StagingManager;
import RendererVK.Buffer;

MeshDataManager::MeshDataManager()
{
}

MeshDataManager::~MeshDataManager()
{
}

bool MeshDataManager::initialize(StagingManager& stagingManager, size_t vertexBufSize, size_t indexBufSize)
{
    m_stagingManager = &stagingManager;
    m_vertexBufSize = vertexBufSize;
    m_indexBufSize = indexBufSize;

    m_vertexBuffer.initialize(vertexBufSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_indexBuffer.initialize(indexBufSize, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

    return true;
}

size_t MeshDataManager::uploadVertexData(const void* pData, vk::DeviceSize size)
{
    assert(m_vertexBufOffset + size <= m_vertexBufSize);
    const size_t offset = m_vertexBufOffset;
    m_stagingManager->upload(m_vertexBuffer.getBuffer(), size, pData, m_vertexBufOffset);
    m_vertexBufOffset += size;
    assert(m_vertexBufOffset <= m_vertexBufSize);
    return offset;
}

size_t MeshDataManager::uploadIndexData(const void* pData, vk::DeviceSize size)
{
    assert(m_indexBufOffset + size <= m_indexBufSize);
    const size_t offset = m_indexBufOffset;
    m_stagingManager->upload(m_indexBuffer.getBuffer(), size, pData, m_indexBufOffset);
    m_indexBufOffset += size;
    assert(m_indexBufOffset <= m_indexBufSize);
    return offset;
}