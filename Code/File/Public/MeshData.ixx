export module File.MeshData;

import Core;
import Core.AABB;

export struct aiMesh;

export class MeshData
{
public:
	MeshData();
	~MeshData();
	MeshData(const MeshData&) = delete;
	MeshData(MeshData&&) = default;

	bool initialize(const aiMesh* pMesh);
	glm::vec3* getVertices();
	glm::vec3* getNormals();
	glm::vec3* getTexCoords();
	uint32_t* getIndices();
	uint32_t getNumVertices();
	uint32_t getNumIndices();
	uint32_t getMaterialIndex();
	AABB getAABB();
	void getIndices(std::vector<uint32_t>& indices);
	const char* getName() const { return m_pName; }

private:

	const char* m_pName = nullptr;
	const aiMesh* m_pMesh = nullptr;
	std::vector<uint32_t> m_indices;
};