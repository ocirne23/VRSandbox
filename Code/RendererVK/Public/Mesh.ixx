export module RendererVK.Mesh;

import Core;
import RendererVK;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.GraphicsPipeline;
import File.MeshData;

export class MeshDataManager;
export class MeshInstance;

export struct InstanceData
{
	glm::vec3 pos;
	float scale;
	glm::quat rot;
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

	bool initialize(MeshData& meshData);

	static VertexLayoutInfo getVertexLayoutInfo();
	uint32 getNumIndices() const { return m_numIndices; }
	uint32 getVertexOffset() const { return m_vertexOffset; }
	uint32 getIndexOffset() const { return m_indexOffset; }

	void setInstanceCapacity(uint32 capacity);
	void addInstance(MeshInstance* pInstance);
	void removeInstance(MeshInstance* pInstance);

	void setInstanceData(uint32 instanceIdx, InstanceData* pInstanceData);

private:
	
	uint32 m_numIndices;
	uint32 m_vertexOffset = 0;
	uint32 m_indexOffset = 0;

	friend class RendererVK;
	uint32 m_indirectCommandIdx = 0;

	std::vector<InstanceData> m_instanceData;
	std::vector<MeshInstance*> m_instances;
};