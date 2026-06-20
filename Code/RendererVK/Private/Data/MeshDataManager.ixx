export module RendererVK:MeshDataManager;

import Core;
import :VK;
import :Layout;
import :Buffer;

export class MeshDataManager final
{
public:

    MeshDataManager();
    ~MeshDataManager();
    MeshDataManager(const MeshDataManager&) = delete;

    bool initialize(size_t vertexBufSize, size_t indexBufSize);

    Buffer& getVertexBuffer() { return m_vertexBuffer; }
    Buffer& getIndexBuffer() { return m_indexBuffer; }
    Buffer& getSkinningBuffer() { return m_skinningBuffer; }

    size_t getVertexBufSize() const { return m_vertexBufSize; }
    size_t getIndexBufSize() const { return m_indexBufSize; }

    size_t getVertexBufUsed() const { return m_vertexBufOffset; }
    size_t getIndexBufUsed() const { return m_indexBufOffset; }

    // Bumped whenever a mega-buffer is reallocated (capacity growth). The Renderer compares this each
    // frame and re-records its command buffers when it changes (they bind the buffers by handle).
    uint32 getGeneration() const { return m_generation; }

private:

    friend class ObjectContainer;
    size_t uploadVertexData(const void* pData, size_t size);
    size_t uploadIndexData(const void* pData, size_t size);
    size_t uploadSkinningData(const void* pData, size_t size);
    // Reserves (uninitialized) space in the vertex mega-buffer for a per-instance skinned output region;
    // the skinning compute fills it each frame. Returns the byte offset (caller divides by sizeof(MeshVertex)).
    size_t reserveVertexData(size_t size);

    // Doubles the buffer until neededSize fits, GPU-copying the used range into the new allocation.
    void growBuffer(Buffer& buffer, size_t& bufSize, size_t usedSize, size_t neededSize, vk::BufferUsageFlags2 usage);

private:

    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;
    Buffer m_skinningBuffer;
    uint32 m_generation = 0;

    size_t m_vertexBufSize = 0;
    size_t m_indexBufSize = 0;
    size_t m_skinningBufSize = 0;

    size_t m_vertexBufOffset = 0;
    size_t m_indexBufOffset = 0;
    size_t m_skinningBufOffset = 0;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU4")
    MeshDataManager meshDataManager;
#pragma warning(default: 4075)
}