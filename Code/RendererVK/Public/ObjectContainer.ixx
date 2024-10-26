export module RendererVK.ObjectContainer;

import Core;
import Core.glm;
import RendererVK.Layout;

export class SceneData;
export class MeshData;
export class MaterialData;
export class NodeData;
export class RenderNode;
export struct Transform;

export class ObjectContainer final
{
public:

    friend class RendererVK;
    friend class ObjectSpawner;
    friend class RenderNode;
    friend class IndirectCullComputePipeline;

    ObjectContainer() {}
    ~ObjectContainer() {}
    ObjectContainer(const ObjectContainer&) = delete;

    bool initialize(const SceneData& sceneData);
    bool isValid() const { return !m_meshInfos.empty(); }

private:

    void updateRenderTransform(RenderNode& node);
    void updateAllRenderTransforms();
    void updateRenderTransforms(uint32 startIdx, uint32 numNodes);

    void initializeMeshes(const std::vector<MeshData>& meshData);
    void initializeMaterials(const std::vector<MaterialData>& materialData);
    RenderNode addNodes(const std::vector<RendererVKLayout::LocalSpaceNode>& nodes, glm::vec3 pos, float scale, glm::quat quat);
    uint32 addMeshInstance(uint32 meshIdx);

private:

    std::vector<RendererVKLayout::LocalSpaceNode> m_renderNodes;
    std::vector<RendererVKLayout::MeshInfo> m_meshInfos;
    std::vector<RendererVKLayout::MaterialInfo> m_materialInfos;
    std::vector<uint16> m_materialIdxForMeshIdx;

    std::vector<uint16> m_numMeshInstancesForInfo;
    std::vector<std::span<RendererVKLayout::MeshInstance>> m_meshInstancesForInfo;

    uint32 m_baseMeshInfoIdx = 0;
    uint32 m_baseMaterialInfoIdx = 0;

    std::vector<std::string> m_meshNames;
    std::vector<std::string> m_materialNames;
};