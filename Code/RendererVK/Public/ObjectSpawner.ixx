export module RendererVK.ObjectSpawner;

import Core;
import Core.glm;
import RendererVK.Layout;
import RendererVK.RenderNode;

export class NodeData;
export class ObjectContainer;

export class ObjectSpawner final
{
public:

    ObjectSpawner() {}
    ~ObjectSpawner() {}
    ObjectSpawner(const ObjectSpawner&) = delete;

    bool initialize(ObjectContainer& container, const NodeData& nodeData);

    RenderNode spawn(glm::vec3 pos, float scale, glm::quat quat);

private:

    ObjectContainer* m_pContainer = nullptr;
    std::vector<RendererVKLayout::LocalSpaceNode> m_initialStateNodes;
    //std::vector<std::string> m_nodeNames;
};