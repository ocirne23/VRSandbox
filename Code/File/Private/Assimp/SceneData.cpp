module;

#include <unordered_map>

module File.SceneData;

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
        [[maybe_unused]] bool res = mesh.initialize(m_pScene->mMeshes[i]);
        assert(res && "Failed to initialize MeshData");
    }

    // --- Unified texture registry ---
    // Embedded textures occupy indices [0, mNumTextures) and are registered first
    // so that material paths of the form "*N" map directly to index N.
    for (uint32 i = 0; i < m_pScene->mNumTextures; i++)
    {
        TextureData& texture = m_textures.emplace_back();
        [[maybe_unused]] bool res = texture.initialize(m_pScene->mTextures[i]);
        assert(res && "Failed to initialize TextureData");
    }

    // Build a path -> index map seeded with embedded textures.
    // Embedded texture paths use Assimp's "*N" convention.
    std::unordered_map<std::string, uint32> texRegistry;
    for (uint32 i = 0; i < (uint32)m_textures.size(); ++i)
        texRegistry[std::string("*") + std::to_string(i)] = i;

    // Resolves one texture slot for a material.
    // Embedded textures are looked up in the registry by "*N" key.
    // Loose file textures are added to m_textures on first encounter and
    // de-duplicated by their raw path string.
    auto resolveTexIdx = [&](const aiMaterial* pMat, aiTextureType type) -> uint32
    {
        aiString path;
        if (pMat->GetTexture(type, 0, &path) != aiReturn_SUCCESS || path.length == 0)
            return UINT32_MAX;

        const std::string pathStr = path.C_Str();
        auto it = texRegistry.find(pathStr);
        if (it != texRegistry.end())
            return it->second;

        if (pathStr[0] == '*')
        {
            // Embedded reference that wasn't pre-registered – shouldn't happen.
            assert(false && "Embedded texture index out of range");
            return UINT32_MAX;
        }

        // Loose file texture not yet seen: add it to the unified list.
        const uint32 newIdx = (uint32)m_textures.size();
        TextureData& tex = m_textures.emplace_back();
        [[maybe_unused]] bool res = tex.initialize(pathStr.c_str());
        assert(res && "Failed to initialize loose TextureData");
        texRegistry[pathStr] = newIdx;
        return newIdx;
    };

    for (uint32 i = 0; i < m_pScene->mNumMaterials; i++)
    {
        MaterialData& material = m_materials.emplace_back();
        [[maybe_unused]] bool res = material.initialize(m_pScene->mMaterials[i]);
        assert(res && "Failed to initialize MaterialData");

        const aiMaterial* pMat = m_pScene->mMaterials[i];
        material.setDiffuseTexIdx(resolveTexIdx(pMat, aiTextureType_DIFFUSE));
        material.setNormalTexIdx(resolveTexIdx(pMat, aiTextureType_NORMALS));
        material.setOpacityTexIdx(resolveTexIdx(pMat, aiTextureType_OPACITY));
        // glTF combined metallic-roughness texture (roughness=G, metalness=B)
        material.setMetalRoughnessTexIdx(resolveTexIdx(pMat, aiTextureType_UNKNOWN));
    }

    m_rootNode.initialize(m_pScene->mRootNode);

    return m_pScene != nullptr;
}

const IMeshData* SceneData::getMesh(const char* pMeshName) const
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