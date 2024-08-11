export module File.SceneData;

import Core;
import File.Assimp;
import File.MeshData;
import File.TextureData;
import File.MaterialData;
import File.NodeData;

export struct aiScene;

export class SceneData final
{
public:
    SceneData();
    ~SceneData();
    SceneData(const SceneData&) = delete;
    SceneData(SceneData&&) = default;

    bool initialize(const char* filePath);

    const std::string& getFilePath() const { return m_filePath; }
    const std::vector<MeshData>& getMeshes() const { return m_meshes; }
    const MeshData* getMesh(const char* pMeshName) const;
    const NodeData& getRootNode() const { return m_rootNode; }

    const MaterialData& getMaterial(uint32 materialIdx) const;

private:

    std::string m_filePath;
    Assimp::Importer m_importer;
    const aiScene* m_pScene = nullptr;
    std::vector<MeshData> m_meshes;
    std::vector<TextureData> m_textures;
    std::vector<MaterialData> m_materials;
    NodeData m_rootNode;
};