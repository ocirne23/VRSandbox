export module RendererVK.ObjectContainer;

import Core;
import Core.glm;
import RendererVK.MeshInstance;
import RendererVK.RenderNode;
import RendererVK.Layout;

export class SceneData;
export class NodeData;

export class ObjectContainer final
{
public:

    friend class RendererVK;

    ObjectContainer() {}
    ~ObjectContainer() {}
    ObjectContainer(const ObjectContainer&) = delete;

    bool initialize(const char* filePath);
    uint32 createNewRootInstance(glm::vec3 pos, float scale, glm::quat quat);

    void updateInstancePositions(uint32 nodeIdx);
    void updateAllInstancePositions();

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

    std::vector<RenderNode> m_renderNodes;
    std::vector<RenderNode> m_initialStateNodes;
    std::vector<WorldSpaceNode> m_worldSpaceNodes;
};