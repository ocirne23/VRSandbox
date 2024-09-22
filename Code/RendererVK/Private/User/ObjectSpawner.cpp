module RendererVK.ObjectSpawner;

import File.Assimp;
import File.NodeData;
import RendererVK.ObjectContainer;

bool ObjectSpawner::initialize(ObjectContainer& container, const NodeData& nodeData)
{
    assert(nodeData.isValid());
    if (!nodeData.isValid())
        return false;

    m_pContainer = &container;

    std::list<std::pair<const aiNode*, int>> nodeDataParentIdxStack;
    nodeDataParentIdxStack.push_back({ nodeData.getAiNode(), 0 });

    while (!nodeDataParentIdxStack.empty())
    {
        auto [pAiNode, parentIdx] = nodeDataParentIdxStack.front();
        nodeDataParentIdxStack.pop_front();
        bool isRoot = nodeData.getAiNode() == pAiNode;

        aiVector3f pos, scale;
        aiQuaternion rot;
        pAiNode->mTransformation.Decompose(scale, rot, pos);

        const float nonUniformScaleAmount = glm::max(glm::distance(scale.x, scale.y), glm::distance(scale.x, scale.z));
        assert(nonUniformScaleAmount < 0.0001f && "Non-uniform scaling is not supported");

        const uint32 numChildren = pAiNode->mNumChildren;
        const uint32 nodeIdx = (uint32)m_initialStateNodes.size();

        RendererVKLayout::LocalSpaceNode& node = m_initialStateNodes.emplace_back();
        node.pos   = isRoot ? glm::vec3(0) : glm::vec3(pos.x, pos.y, pos.z);
        node.scale = isRoot ? 1.0f : scale.x;
        node.quat  = isRoot ? glm::quat(1, 0, 0, 0) : glm::quat(rot.w, rot.x, rot.y, rot.z);
        node.numChildren = (uint16)numChildren;
        node.parentOffset = (uint16)(nodeIdx - parentIdx);
        //m_nodeNames.push_back(pAiNode->mName.C_Str());
        if (pAiNode->mNumMeshes > 0)
        {
            node.meshInfoIdx = pAiNode->mMeshes[0];

            // If the node has more than 1 mesh, add the remaining meshes as children of the current node
            if (pAiNode->mNumMeshes > 1)
                node.numChildren += (uint16)(pAiNode->mNumMeshes - 1);

            for (uint32 i = 1; i < pAiNode->mNumMeshes; ++i)
            {
                const uint32 childNodeIdx = (uint32)m_initialStateNodes.size();
                RendererVKLayout::LocalSpaceNode& childNode = m_initialStateNodes.emplace_back();
                childNode.pos = glm::vec3(0.0f);
                childNode.scale = 1.0f;
                childNode.quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childNode.parentOffset = (uint16)(childNodeIdx - nodeIdx);
                childNode.numChildren = 0;
                childNode.meshInfoIdx = pAiNode->mMeshes[i];
                //m_nodeNames.push_back(std::string(pAiNode->mName.C_Str()) + ":" + std::to_string(i));
            }
        }
        else
        {
            node.meshInfoIdx = USHRT_MAX;
        }

        for (uint32 i = 0; i < numChildren; ++i)
        {
            nodeDataParentIdxStack.push_back({ pAiNode->mChildren[i], nodeIdx });
        }
    }
    return true;
}

RenderNode ObjectSpawner::spawn(glm::vec3 pos, float scale, glm::quat quat)
{
    return m_pContainer->addNodes(m_initialStateNodes, pos, scale, quat);
}