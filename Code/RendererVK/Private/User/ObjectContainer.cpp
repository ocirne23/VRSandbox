module RendererVK.ObjectContainer;

import File.SceneData;
import File.NodeData;
import RendererVK;
import RendererVK.MeshDataManager;
import RendererVK.RenderNode;

bool ObjectContainer::initialize(const char* filePath)
{
    SceneData sceneData;
    if (!sceneData.initialize(filePath))
    {
        return false;
    }

    m_filePath = filePath;

    MeshDataManager& meshDataManager = VK::g_renderer.m_meshDataManager;

    const auto& meshDataList = sceneData.getMeshes();
    const uint32 numMeshes = (uint32)meshDataList.size();
    m_baseMeshInfoIdx = VK::g_renderer.registerObjectContainer(this, numMeshes);

    m_meshInstances.resize(meshDataList.size());
    m_meshInfos.reserve(numMeshes);

    for (const MeshData& meshData : meshDataList)
    {
        RendererVKLayout::MeshInfo& meshInfo = m_meshInfos.emplace_back();

        std::vector<RendererVKLayout::MeshVertex> vertices;
        vertices.resize(meshData.getNumVertices());
        const glm::vec3* pVertices = meshData.getVertices();
        const glm::vec3* pNormals = meshData.getNormals();
        const glm::vec3* pTexCoords = meshData.getTexCoords();
        const glm::vec3* pTangents = meshData.getTangents();
        const glm::vec3* pBitangents = meshData.getBitangents();

        for (uint32 i = 0; i < meshData.getNumVertices(); i++)
        {
            vertices[i].position = pVertices[i];
            vertices[i].normal = pNormals[i];
            const float handedness = glm::dot(pNormals[i], glm::cross(pTangents[i], pBitangents[i])) >= 0.0f ? 1.0f : -1.0f;
            vertices[i].tangent = glm::vec4(pTangents[i], handedness);
            vertices[i].texCoord = glm::vec2(pTexCoords[i]);
        }

        std::vector<uint32> indices;
        meshData.getIndices(indices);
        meshInfo.indexCount   = (uint32)indices.size();
        meshInfo.vertexOffset = (int32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(RendererVKLayout::MeshVertex)) / sizeof(RendererVKLayout::MeshVertex));
        meshInfo.firstIndex   = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(RendererVKLayout::MeshIndex)) / sizeof(RendererVKLayout::MeshIndex));
        meshInfo.radius       = meshData.getAABB().getRadius();
        meshInfo.center       = meshData.getAABB().getCenter();
    }

    initializeNodes(sceneData.getRootNode());

    return true;
}

uint32 ObjectContainer::addMeshInstance(uint32 meshIdx)
{
    const uint32 meshInstanceIdx = (uint32)m_meshInstances[meshIdx].size();
    MeshInstance& meshInstance = m_meshInstances[meshIdx].emplace_back();
    meshInstance.meshInfoIdx = m_baseMeshInfoIdx + meshIdx;
    return meshInstanceIdx;
}

void ObjectContainer::updateInstancePositions()
{
    if (m_initialStateNodes.empty())
        return;

    WorldSpaceNode* parentNode = &m_worldSpaceNodes[0];

    {   // Update root node and its mesh instance if any
        const RenderNode& node = m_initialStateNodes[0];
        memcpy(&m_worldSpaceNodes[0], &m_initialStateNodes[0], sizeof(WorldSpaceNode));
        if (node.m_meshInstanceIdx != USHRT_MAX)
        {
            MeshInstance& meshInstance = m_meshInstances[node.m_meshInfoIdx][node.m_meshInstanceIdx];
            meshInstance.pos = node.m_pos;
            meshInstance.scale = node.m_scale;
            meshInstance.quat = node.m_quat;
        }
    }

    for (uint32 i = 1; i < m_worldSpaceNodes.size(); i++)
    {
        const RenderNode& node = m_initialStateNodes[i];
        parentNode = &m_worldSpaceNodes[node.m_parentIdx];
        WorldSpaceNode& nodeWS = m_worldSpaceNodes[i];
        nodeWS.pos   = parentNode->pos + parentNode->quat * (node.m_pos * parentNode->scale);
        nodeWS.quat  = parentNode->quat * node.m_quat;
        nodeWS.scale = parentNode->scale * node.m_scale;
        if (node.m_meshInstanceIdx != USHRT_MAX)
        {
            MeshInstance& meshInstance = m_meshInstances[node.m_meshInfoIdx][node.m_meshInstanceIdx];
            meshInstance.pos   = nodeWS.pos;
            meshInstance.scale = nodeWS.scale;
            meshInstance.quat  = nodeWS.quat;
        }
    }
}

void ObjectContainer::initializeNodes(const NodeData& rootNodeData)
{
    std::list<std::pair<const aiNode*, uint32>> nodeDataParentIdxStack;
    nodeDataParentIdxStack.push_back({ rootNodeData.getAiNode(), 0});

    while (!nodeDataParentIdxStack.empty())
    {
        auto [pAiNode, parentIdx] = nodeDataParentIdxStack.front();
        nodeDataParentIdxStack.pop_front();

        aiVector3f pos;
        aiVector3f scale;
        aiQuaternion rot;
        pAiNode->mTransformation.Decompose(scale, rot, pos);

        const float nonUniformScaleAmount = glm::max(glm::distance(scale.x, scale.y), glm::distance(scale.x, scale.z));
        assert(nonUniformScaleAmount < 0.0001f && "Non-uniform scaling is not supported");

        const uint32 numChildren = pAiNode->mNumChildren;
        const uint32 nodeIdx     = (uint32)m_initialStateNodes.size();

        RenderNode& renderNode   = m_initialStateNodes.emplace_back();
        renderNode.m_pos         = glm::vec3(pos.x, pos.y, pos.z);
        renderNode.m_scale       = scale.x;
        renderNode.m_quat        = glm::quat(rot.w, rot.x, rot.y, rot.z);
        renderNode.m_numChildren = (uint16)numChildren;
        renderNode.m_parentIdx   = (uint16)parentIdx;

        // If the node has more than 1 mesh, add the remaining meshes as children of the current node
        if (pAiNode->mNumMeshes > 0)
        {
            renderNode.m_meshInfoIdx = pAiNode->mMeshes[0];
            renderNode.m_meshInstanceIdx = addMeshInstance(pAiNode->mMeshes[0]);

            if (pAiNode->mNumMeshes > 1)
            {
                renderNode.m_numChildren += (uint16)(pAiNode->mNumMeshes - 1);
            }
            for (uint32 i = 1; i < pAiNode->mNumMeshes; i++)
            {
                RenderNode& childNode = m_initialStateNodes.emplace_back();
                childNode.m_pos = glm::vec3(0.0f);
                childNode.m_scale = 1.0f;
                childNode.m_quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childNode.m_parentIdx = (uint16)nodeIdx;
                childNode.m_numChildren = 0;
                childNode.m_meshInfoIdx = pAiNode->mMeshes[i];
                childNode.m_meshInstanceIdx = addMeshInstance(pAiNode->mMeshes[i]);
            }
        }
        else
        {
            renderNode.m_meshInfoIdx = USHRT_MAX;
            renderNode.m_meshInstanceIdx = USHRT_MAX;
        }

        for (uint32 i = 0; i < numChildren; i++)
        {
            nodeDataParentIdxStack.push_back({ pAiNode->mChildren[i], nodeIdx});
        }
    }
    m_worldSpaceNodes.resize(m_initialStateNodes.size());
}