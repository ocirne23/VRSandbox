export module File.MeshData;
extern "C++" {

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
    const glm::vec3* getVertices() const;
    const glm::vec3* getNormals() const;
    const glm::vec3* getTangents() const;
    const glm::vec3* getBitangents() const;
    const glm::vec3* getTexCoords() const;
    const uint32* getIndices() const;
    uint32 getNumVertices() const;
    uint32 getNumIndices() const;
    uint32 getMaterialIndex() const;
    AABB getAABB() const;
    void getIndices(std::vector<uint32>& indices) const;
    const char* getName() const { return m_pName; }

private:

    const char* m_pName = nullptr;
    const aiMesh* m_pMesh = nullptr;
    std::vector<uint32> m_indices;
};
} // extern "C++"