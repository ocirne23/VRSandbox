export module RendererVK.MeshDataManager;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;

export class StagingManager;

export class MeshDataManager final
{
public:

	MeshDataManager();
	~MeshDataManager();
	MeshDataManager(const MeshDataManager&) = delete;

	bool initialize(StagingManager& stagingManager, size_t vertexBufSize, size_t indexBufSize);
	size_t uploadVertexData(const void* pData, vk::DeviceSize size);
	size_t uploadIndexData(const void* pData, vk::DeviceSize size);

	Buffer& getVertexBuffer() { return m_vertexBuffer; }
	Buffer& getIndexBuffer() { return m_indexBuffer; }

	size_t getVertexBufSize() const { return m_vertexBufSize; }
	size_t getIndexBufSize() const { return m_indexBufSize; }

	size_t getVertexBufUsed() const { return m_vertexBufOffset; }
	size_t getIndexBufUsed() const { return m_indexBufOffset; }

private:

	StagingManager* m_stagingManager = nullptr;

	Buffer m_vertexBuffer;
	Buffer m_indexBuffer;

	size_t m_vertexBufSize = 0;
	size_t m_indexBufSize = 0;

	size_t m_vertexBufOffset = 0;
	size_t m_indexBufOffset = 0;
};