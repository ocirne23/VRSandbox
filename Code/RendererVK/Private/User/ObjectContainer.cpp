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
        return false;

    m_filePath = filePath;

    VK::g_renderer.addObjectContainer(this);

    initializeMaterials(sceneData.getMaterials());
    initializeMeshes(sceneData.getMeshes());
    initializeNodes(sceneData.getRootNode());

    return true;
}

void ObjectContainer::initializeMeshes(const std::vector<MeshData>& meshDataList)
{
    const size_t numMeshes = meshDataList.size();

    m_meshInstances.resize(numMeshes);
    m_meshInfos.reserve(numMeshes);

    MeshDataManager& meshDataManager = VK::g_renderer.m_meshDataManager;

    for (const MeshData& meshData : meshDataList)
    {
        RendererVKLayout::MeshInfo& meshInfo = m_meshInfos.emplace_back();
        m_meshNames.push_back(meshData.getName());
        m_materialIdxForMeshIdx.push_back(meshData.getMaterialIndex());

        std::vector<RendererVKLayout::MeshVertex> vertices;
        vertices.resize(meshData.getNumVertices());
        const glm::vec3* pVertices = meshData.getVertices();
        const glm::vec3* pNormals = meshData.getNormals();
        const glm::vec3* pTexCoords = meshData.getTexCoords();
        const glm::vec3* pTangents = meshData.getTangents();
        const glm::vec3* pBitangents = meshData.getBitangents();

        for (uint32 i = 0; i < meshData.getNumVertices(); i++)
        {
            vertices[i].position   = pVertices[i];
            vertices[i].normal     = pNormals[i];
            const float handedness = glm::dot(pNormals[i], glm::cross(pTangents[i], pBitangents[i])) >= 0.0f ? 1.0f : -1.0f;
            vertices[i].tangent    = glm::vec4(pTangents[i], handedness);
            vertices[i].texCoord   = glm::vec2(pTexCoords[i]);
        }

        std::vector<uint32> indices;
        meshData.getIndices(indices);
        meshInfo.indexCount   = (uint32)indices.size();
        meshInfo.vertexOffset = (int32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(RendererVKLayout::MeshVertex)) / sizeof(RendererVKLayout::MeshVertex));
        meshInfo.firstIndex   = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(RendererVKLayout::MeshIndex)) / sizeof(RendererVKLayout::MeshIndex));
        meshInfo.radius       = meshData.getAABB().getRadius();
        meshInfo.center       = meshData.getAABB().getCenter();
    }

    m_baseMeshInfoIdx = VK::g_renderer.addMeshInfos(m_meshInfos);
}

union MaterialFlags
{
    struct {
        uint32 baseColorTex    : 1;
        uint32 hasNormalTex    : 1;
        uint32 hasRoughnessTex : 1;
        uint32 hasMetalnessTex : 1;
        uint32 hasOcclusionTex : 1;
        uint32 hasEmissiveTex  : 1;
        uint32 hasOpacityTex   : 1;
        uint32 hasSpecularTex  : 1;
        uint32 remainder       : 24;
    } bits;
    uint32 flags;
};

void ObjectContainer::initializeMaterials(const std::vector<MaterialData>& materialDataList)
{
    const size_t numMaterials = materialDataList.size();
    m_materialInfos.reserve(numMaterials);
    m_materialNames.reserve(numMaterials);

    for (const MaterialData& materialData : materialDataList)
    {
        RendererVKLayout::MaterialInfo& material = m_materialInfos.emplace_back();

        material.baseColor     = materialData.getBaseColor();
        material.roughness     = materialData.getRoughnessFactor();
        material.specularColor = materialData.getSpecularColor();
        material.metalness     = materialData.getMetalnessFactor();
        material.emissiveColor = materialData.getEmissiveColor() * materialData.getEmissiveIntensity();
        material.flags         = 0;

        m_materialNames.push_back(materialData.getName());
    }

    m_baseMaterialInfoIdx = VK::g_renderer.addMaterialInfos(m_materialInfos);
}

void ObjectContainer::initializeNodes(const NodeData& rootNodeData)
{
    std::list<std::pair<const aiNode*, uint32>> nodeDataParentIdxStack;
    nodeDataParentIdxStack.push_back({ rootNodeData.getAiNode(), 0 });

    while (!nodeDataParentIdxStack.empty())
    {
        auto [pAiNode, parentIdx] = nodeDataParentIdxStack.front();
        nodeDataParentIdxStack.pop_front();

        aiVector3f pos, scale;
        aiQuaternion rot;
        pAiNode->mTransformation.Decompose(scale, rot, pos);

        const float nonUniformScaleAmount = glm::max(glm::distance(scale.x, scale.y), glm::distance(scale.x, scale.z));
        assert(nonUniformScaleAmount < 0.0001f && "Non-uniform scaling is not supported");

        const uint32 numChildren = pAiNode->mNumChildren;
        const uint32 nodeIdx     = (uint32)m_initialStateNodes.size();

        LocalSpaceNode& node = m_initialStateNodes.emplace_back();
        node.pos          = glm::vec3(pos.x, pos.y, pos.z);
        node.scale        = scale.x;
        node.quat         = glm::quat(rot.w, rot.x, rot.y, rot.z);
        node.numChildren  = (uint16)numChildren;
        node.parentOffset = (uint16)(nodeIdx - parentIdx);
        m_nodeNames.push_back(pAiNode->mName.C_Str());

        if (pAiNode->mNumMeshes > 0)
        {
            node.meshInfoIdx = pAiNode->mMeshes[0];

            // If the node has more than 1 mesh, add the remaining meshes as children of the current node
            if (pAiNode->mNumMeshes > 1)
                node.numChildren += (uint16)(pAiNode->mNumMeshes - 1);

            for (uint32 i = 1; i < pAiNode->mNumMeshes; ++i)
            {
                const uint32 childNodeIdx = (uint32)m_initialStateNodes.size();
                LocalSpaceNode& childNode = m_initialStateNodes.emplace_back();
                childNode.pos          = glm::vec3(0.0f);
                childNode.scale        = 1.0f;
                childNode.quat         = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childNode.parentOffset = (uint16)(childNodeIdx - nodeIdx);
                childNode.numChildren  = 0;
                childNode.meshInfoIdx  = pAiNode->mMeshes[i];
                m_nodeNames.push_back(m_meshNames[pAiNode->mMeshes[i]]);
            }
        }
        else
        {
            node.meshInfoIdx = USHRT_MAX;
        }

        for (uint32 i = 0; i < numChildren; ++i)
        {
            nodeDataParentIdxStack.push_back({ pAiNode->mChildren[i], nodeIdx});
        }
    }
}

uint32 ObjectContainer::addMeshInstance(uint32 meshIdx)
{
    const uint32 meshInstanceIdx = (uint32)m_meshInstances[meshIdx].size();
    assert(meshInstanceIdx < USHRT_MAX && "Too many mesh instances for this mesh type");
    RendererVKLayout::MeshInstance& meshInstance = m_meshInstances[meshIdx].emplace_back();
    meshInstance.meshInfoIdx = m_baseMeshInfoIdx + meshIdx;
    meshInstance.materialInfoIdx = m_baseMaterialInfoIdx + m_materialIdxForMeshIdx[meshIdx];
    return meshInstanceIdx;
}

void ObjectContainer::updateRenderTransform(RenderNode& renderNode)
{
    const uint32 numNodes = (uint32)renderNode.m_numNodes;
    const uint32 startIdx = renderNode.m_nodeIdx;
    for (uint32 i = 0; i < numNodes; ++i)
    {
        const LocalSpaceNode& node       = m_renderNodes[startIdx + i];
        const WorldSpaceNode* parentNode = (node.parentOffset != 0) ? &m_worldSpaceNodes[i - node.parentOffset] : nullptr;
        WorldSpaceNode& nodeWS           = m_worldSpaceNodes[i];
        nodeWS.pos   = parentNode ? parentNode->pos + parentNode->quat * (node.pos * parentNode->scale) : node.pos;
        nodeWS.quat  = parentNode ? parentNode->quat * node.quat : node.quat;
        nodeWS.scale = parentNode ? parentNode->scale * node.scale : node.scale;
        if (node.meshInfoIdx != USHRT_MAX)
        {
            RendererVKLayout::MeshInstance& meshInstance = m_meshInstances[node.meshInfoIdx][node.meshInstanceIdx];
            meshInstance.pos   = nodeWS.pos;
            meshInstance.scale = nodeWS.scale;
            meshInstance.quat  = nodeWS.quat;
        }
    }
}

RenderNode ObjectContainer::cloneNode(RenderNode& node, glm::vec3 pos, float scale, glm::quat quat)
{
    assert(!m_initialStateNodes.empty());
    const size_t copyIdx  = node.m_nodeIdx;
    const size_t numNodes = node.m_numNodes;
    const size_t startIdx = m_renderNodes.size();
    m_renderNodes.resize(startIdx + numNodes);

    memcpy(&m_renderNodes[startIdx], &m_renderNodes[copyIdx], numNodes * sizeof(m_renderNodes[0]));

    m_renderNodes[startIdx].pos   = pos;
    m_renderNodes[startIdx].scale = scale;
    m_renderNodes[startIdx].quat  = quat;

    for (size_t i = startIdx; i < startIdx + numNodes; ++i)
    {
        LocalSpaceNode& node = m_renderNodes[i];
        if (node.meshInfoIdx != USHRT_MAX)
            node.meshInstanceIdx = addMeshInstance(node.meshInfoIdx);
    }

    return RenderNode(this, (uint32)startIdx, (uint16)numNodes);
}

RenderNode ObjectContainer::createNewRootNode(glm::vec3 pos, float scale, glm::quat quat)
{
    assert(!m_initialStateNodes.empty());
    const size_t startIdx = m_renderNodes.size();
    const size_t numNodes = m_initialStateNodes.size();
    m_renderNodes.resize(startIdx + numNodes);
    m_worldSpaceNodes.resize(numNodes);

    memcpy(&m_renderNodes[startIdx], m_initialStateNodes.data(), numNodes * sizeof(m_initialStateNodes[0]));

    m_renderNodes[startIdx].pos   = pos;
    m_renderNodes[startIdx].scale = scale;
    m_renderNodes[startIdx].quat  = quat;
    
    for (size_t i = startIdx; i < startIdx + numNodes; ++i)
    {
        LocalSpaceNode& node = m_renderNodes[i];
        if (node.meshInfoIdx != USHRT_MAX)
            node.meshInstanceIdx = addMeshInstance(node.meshInfoIdx);
    }

    return RenderNode(this, (uint32)startIdx, (uint16)numNodes);
}

/*
RenderNode ObjectContainer::createNewRootFromChildNode(const char* path, const glm::vec3 pos, float scale, glm::quat quat)
{

}*/

