module RendererVK.ObjectContainer;

import File.SceneData;
import RendererVK.Mesh;

bool ObjectContainer::initialize(SceneData& sceneData)
{
    std::vector<MeshData>& meshDataList = sceneData.getMeshes();
    m_meshes.reserve(meshDataList.size());
    for (MeshData& meshData : meshDataList)
    {
        Mesh& mesh = m_meshes.emplace_back();
        if (!mesh.initialize(meshData))
        {
            return false;
        }
    }
    return true;
}