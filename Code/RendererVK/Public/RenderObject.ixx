export module RendererVK.RenderObject;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.Pipeline;
import File.MeshData;

export class MeshDataManager;

export class RenderObject final
{
public:

	struct VertexLayout
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texCoord;
	};

	using IndexLayout = uint32;

	struct InstanceData
	{
		glm::vec3 pos;
		float scale;
		glm::quat rot;
	};

	RenderObject();
	~RenderObject();
	RenderObject(const RenderObject&) = delete;

	bool initialize(MeshDataManager& meshDataManager, MeshData& meshData);

	static VertexLayoutInfo getVertexLayoutInfo();
	uint32 getNumIndices() const { return m_numIndices; }
	uint32 getVertexOffset() const { return m_vertexOffset; }
	uint32 getIndexOffset() const { return m_indexOffset; }

private:
	
	uint32 m_numIndices;
	uint32 m_vertexOffset = 0;
	uint32 m_indexOffset = 0;
};