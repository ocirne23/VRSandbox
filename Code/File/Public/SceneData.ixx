export module File.SceneData;

import Core;
import File.Assimp;
import File.MeshData;
import File.TextureData;
import File.MaterialData;

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
    std::vector<MeshData>& getMeshes() { return m_meshes; }
    MeshData* getMesh(const char* pMeshName);
    MaterialData& getMaterial(uint32 materialIdx);

private:

    std::string m_filePath;
    Assimp::Importer m_importer;
    const aiScene* m_pScene = nullptr;
    std::vector<MeshData> m_meshes;
    std::vector<TextureData> m_textures;
    std::vector<MaterialData> m_materials;
};