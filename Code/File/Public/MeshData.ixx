export module File.MeshData;

import Core;
import Core.glm;
import Core.AABB;

export struct aiMesh;

export class MeshData final
{
public:
    MeshData();
    ~MeshData();
    MeshData(const MeshData&) = delete;
    MeshData(MeshData&&) = default;

    bool initialize(const aiMesh* pMesh);
    void destroy();

	const glm::vec3* getVertices() const { return m_vertices.get(); }
	const glm::vec3* getNormals() const { return m_normals.get(); }
	const glm::vec3* getTangents() const { return m_tangents.get(); }
	const glm::vec3* getBitangents() const { return m_bitangents.get(); }
	const glm::vec3* getTexCoords() const { return m_texCoords.get(); }
	const uint32* getIndices() const { return m_indices.data(); }
	uint32 getNumVertices() const { return m_numVertices; }
	uint32 getNumIndices() const { return static_cast<uint32>(m_indices.size()); }
	uint32 getMaterialIndex() const { return m_materialIdx; }
	const AABB& getAABB() const { return m_aabb; }
    const char* getName() const { return m_name.c_str(); }

private:

    bool m_ownsData = true;
    AABB m_aabb;
    std::vector<uint32> m_indices;
	uint32 m_numVertices = 0;
    std::unique_ptr<glm::vec3[]> m_vertices;
    std::unique_ptr<glm::vec3[]> m_normals;
    std::unique_ptr<glm::vec3[]> m_tangents;
    std::unique_ptr<glm::vec3[]> m_bitangents;
    std::unique_ptr<glm::vec3[]> m_texCoords;
	uint32 m_materialIdx = UINT32_MAX;
    std::string m_name;
};