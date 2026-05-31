export module File.MeshData;

import Core;
import Core.glm;
import Core.AABB;
import File.IMeshData;

export struct aiMesh;

export class MeshData final : public IMeshData
{
public:
    MeshData();
    ~MeshData();
    MeshData(const MeshData&) = delete;
    MeshData(MeshData&&) = default;

    bool initialize(const aiMesh* pMesh);
    const glm::vec3* getVertices() const override;
    const glm::vec3* getNormals() const override;
    const glm::vec3* getTangents() const override;
    const glm::vec3* getBitangents() const override;
    const glm::vec3* getTexCoords() const override;
    const uint32* getIndices() const override;
    uint32 getNumVertices() const override;
    uint32 getNumIndices() const override;
    uint32 getMaterialIndex() const override;
    AABB getAABB() const override;
    const char* getName() const override { return m_pName; }

private:

    const char* m_pName = nullptr;
    const aiMesh* m_pMesh = nullptr;
    std::vector<uint32> m_indices;
};