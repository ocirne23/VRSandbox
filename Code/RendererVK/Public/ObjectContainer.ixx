export module RendererVK.ObjectContainer;

import Core;
import Core.glm;
import RendererVK.Layout;

export class RenderNode;
export class MeshData;
export class MaterialData;
export class NodeData;

export class ObjectContainer final
{
public:

    friend class RendererVK;
    friend class IndirectCullComputePipeline;

    ObjectContainer() {}
    ~ObjectContainer() {}
    ObjectContainer(const ObjectContainer&) = delete;

    bool initialize(const char* filePath, bool preTransformVertices);

    RenderNode createNewRootNode(glm::vec3 pos, float scale, glm::quat quat);
    RenderNode cloneNode(RenderNode& node, glm::vec3 pos, float scale, glm::quat quat);
    void updateRenderTransform(RenderNode& node);

private:

    void initializeMeshes(const std::vector<MeshData>& meshData);
    void initializeMaterials(const std::vector<MaterialData>& materialData);
    void initializeNodes(const NodeData& rootNodeData);
    uint32 addMeshInstance(uint32 meshIdx);

private:

    std::vector<RendererVKLayout::MeshInfo> m_meshInfos;
    std::vector<std::vector<RendererVKLayout::MeshInstance>> m_meshInstances;
    std::vector<RendererVKLayout::MaterialInfo> m_materialInfos;


    std::vector<uint16> m_materialIdxForMeshIdx;

    uint32 m_baseMeshInfoIdx = 0;
    uint32 m_baseMaterialInfoIdx = 0;

    struct WorldSpaceNode
    {
        glm::vec3 pos;
        float scale;
        glm::quat quat;
    };

    struct LocalSpaceNode
    {
        glm::vec3 pos;
        float scale;
        glm::quat quat;

        uint16 meshInfoIdx = USHRT_MAX;
        uint16 meshInstanceIdx = USHRT_MAX;
        uint16 numChildren = 0;
        uint16 parentOffset = 0;
    };
    std::vector<LocalSpaceNode> m_renderNodes;
    std::vector<LocalSpaceNode> m_initialStateNodes;
    std::vector<WorldSpaceNode> m_worldSpaceNodes;

    std::vector<std::string> m_meshNames;
    std::vector<std::string> m_nodeNames;
    std::vector<std::string> m_materialNames;
    std::string m_filePath;
};