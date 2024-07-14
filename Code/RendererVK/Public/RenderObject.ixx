export module RendererVK.RenderObject;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.Pipeline;
import File.MeshData;

export class StagingManager;

export class RenderObject final
{
public:

	struct VertexLayout
	{
		glm::vec3 position;
		glm::vec3 normal;
		glm::vec2 texCoord;
	};

	struct InstanceData
	{
		glm::vec3 pos;
		float scale;
		glm::vec4 rot;
	};

	RenderObject();
	~RenderObject();
	RenderObject(const RenderObject&) = delete;

	bool initialize(const Device& device, StagingManager& stagingManager, MeshData& meshData);

	static VertexLayoutInfo getVertexLayoutInfo();
	uint32 getNumIndices() const { return m_numIndices; }

	Buffer m_vertexBuffer;
	Buffer m_indexBuffer;

private:
	
	uint32 m_numIndices;
};