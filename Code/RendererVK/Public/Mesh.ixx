export module RendererVK.Mesh;

import Core;
import RendererVK;
import RendererVK.GraphicsPipeline;
import File.MeshData;

export class MeshInstance;

export struct MeshInfo
{
	float radius = 5.0f;
	uint32 indexCount;
	uint32 firstIndex;
	int32  vertexOffset;
	uint32 firstInstance;
};

export class Mesh final
{
public:

	struct VertexLayout
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texCoord;
	};

	using IndexLayout = uint32;

	Mesh();
	~Mesh();
	Mesh(Mesh&& copy);
	Mesh(const Mesh&) = delete;

	bool initialize(MeshData& meshData, uint32 meshIdx);

	static VertexLayoutInfo getVertexLayoutInfo();
	uint32 getNumIndices() const { return m_info.indexCount; }
	uint32 getVertexOffset() const { return m_info.vertexOffset; }
	uint32 getIndexOffset() const { return m_info.firstIndex; }
	uint32 getMeshIdx() const { return m_meshIdx; }
	float getRadius() const { return m_info.radius; }

	void addInstance(MeshInstance* pInstance);
	void removeInstance(MeshInstance* pInstance);

private:
	friend class RendererVK;

	MeshInfo m_info;
	uint32 m_meshIdx = 0;
	uint32 m_numInstances = 0;
};