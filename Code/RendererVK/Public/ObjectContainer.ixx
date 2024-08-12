export module RendererVK.ObjectContainer;

import Core;
import Core.glm;
import RendererVK.MeshInstance;
import RendererVK.Layout;

export class RenderNode;
export class NodeData;

export class ObjectContainer final
{
public:

    friend class RendererVK;

    ObjectContainer() {}
    ~ObjectContainer() {}
    ObjectContainer(const ObjectContainer&) = delete;

    bool initialize(const char* filePath);

    RenderNode createNewRootInstance(glm::vec3 pos, float scale, glm::quat quat);
    void updateInstancePositions(RenderNode& node);

private:

    void initializeNodes(const NodeData& rootNodeData);
    uint32 addMeshInstance(uint32 meshIdx);

private:

    std::string m_filePath;
    std::vector<std::string> m_meshNames;

    uint32 m_baseMeshInfoIdx = 0;
    std::vector<RendererVKLayout::MeshInfo> m_meshInfos;
    std::vector<std::vector<MeshInstance>> m_meshInstances;

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
};