export module RendererVK.MeshDataManager;
extern "C++" {

import Core;
import RendererVK.Layout;
import RendererVK.Buffer;

export class MeshData;

export class MeshDataManager final
{
public:

    MeshDataManager();
    ~MeshDataManager();
    MeshDataManager(const MeshDataManager&) = delete;

    bool initialize(size_t vertexBufSize, size_t indexBufSize);

    Buffer& getVertexBuffer() { return m_vertexBuffer; }
    Buffer& getIndexBuffer() { return m_indexBuffer; }

    size_t getVertexBufSize() const { return m_vertexBufSize; }
    size_t getIndexBufSize() const { return m_indexBufSize; }

    size_t getVertexBufUsed() const { return m_vertexBufOffset; }
    size_t getIndexBufUsed() const { return m_indexBufOffset; }

private:

    friend class ObjectContainer;
    size_t uploadVertexData(const void* pData, size_t size);
    size_t uploadIndexData(const void* pData, size_t size);

private:

    Buffer m_vertexBuffer;
    Buffer m_indexBuffer;

    size_t m_vertexBufSize = 0;
    size_t m_indexBufSize = 0;

    size_t m_vertexBufOffset = 0;
    size_t m_indexBufOffset = 0;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU4")
    MeshDataManager meshDataManager;
#pragma warning(default: 4075)
}
} // extern "C++"