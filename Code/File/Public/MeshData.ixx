export module File.MeshData;

import Core;
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
    glm::vec3* getVertices();
    glm::vec3* getNormals();
    glm::vec3* getTexCoords();
    uint32* getIndices();
    uint32 getNumVertices();
    uint32 getNumIndices();
    uint32 getMaterialIndex();
    AABB getAABB();
    void getIndices(std::vector<uint32>& indices);
    const char* getName() const { return m_pName; }

private:

    const char* m_pName = nullptr;
    const aiMesh* m_pMesh = nullptr;
    std::vector<uint32> m_indices;
};