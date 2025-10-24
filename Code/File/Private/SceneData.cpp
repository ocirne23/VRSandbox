module File.SceneData;

import File.Assimp;
import File.MeshData;

using namespace Assimp;

SceneData::SceneData()
{
}

SceneData::~SceneData()
{
}

bool SceneData::initialize(const char* filePath, bool mergeNodes, bool preTransformVertices)
{
    uint32 optimizationFlags = aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials | aiProcess_SortByPType;
    if (mergeNodes)
    {
        optimizationFlags |= aiProcess_FindInstances;
        optimizationFlags |= aiProcess_OptimizeMeshes;
        if (!preTransformVertices)
            optimizationFlags |= aiProcess_OptimizeGraph;
    }
    if (preTransformVertices)
        optimizationFlags |= aiProcess_PreTransformVertices;
    //aiProcess_MakeLeftHanded
    m_pScene = m_importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_GenNormals | aiProcess_FlipUVs | aiProcess_GenBoundingBoxes | aiProcess_CalcTangentSpace | optimizationFlags);
    if (!m_pScene)
    {
        assert(false && "Failed to load scene");
        return false;
    }
    m_filePath = filePath;
    for (uint32 i = 0; i < m_pScene->mNumMeshes; i++)
    {
        MeshData& mesh = m_meshes.emplace_back();
        bool res = mesh.initialize(m_pScene->mMeshes[i]);
        assert(res && "Failed to initialize MeshData");
    }
    for (uint32 i = 0; i < m_pScene->mNumTextures; i++)
    {
        TextureData& texture = m_textures.emplace_back();
        bool res = texture.initialize(m_pScene->mTextures[i]);
        assert(res && "Failed to initialize TextureData");
    }
    for (uint32 i = 0; i < m_pScene->mNumMaterials; i++)
    {
        MaterialData& material = m_materials.emplace_back();
        bool res = material.initialize(m_pScene->mMaterials[i]);
        assert(res && "Failed to initialize MaterialData");
    }
    const aiNode* pRootNode = m_pScene->mRootNode;

    m_rootNode.initialize(pRootNode);

    return m_pScene != nullptr;
}

const MeshData* SceneData::getMesh(const char* pMeshName) const
{
    for (const MeshData& mesh : m_meshes)
    {
        if (std::string(mesh.getName()) == pMeshName)
        {
            return &mesh;
        }
    }
    return nullptr;
}

const MaterialData& SceneData::getMaterial(uint32 materialIdx) const
{
    assert(materialIdx < (uint32)m_materials.size());
    return m_materials[materialIdx];
}
