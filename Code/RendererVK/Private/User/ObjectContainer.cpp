module RendererVK.ObjectContainer;

import File.SceneData;
import File.NodeData;
import RendererVK;
import RendererVK.MeshDataManager;
import RendererVK.RenderNode;

bool ObjectContainer::initialize(const SceneData& sceneData)
{
    if (!sceneData.isValid())
        return false;
    Globals::rendererVK.addObjectContainer(this);
    initializeMaterials(sceneData.getMaterials());
    initializeMeshes(sceneData.getMeshes());
    return true;
}

void ObjectContainer::initializeMeshes(const std::vector<MeshData>& meshDataList)
{
    const size_t numMeshes = meshDataList.size();

    m_meshInfos.reserve(numMeshes);
    m_meshInstancesForInfo.resize(numMeshes);
    m_numMeshInstancesForInfo.resize(numMeshes);

    MeshDataManager& meshDataManager = Globals::rendererVK.m_meshDataManager;

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

    m_baseMeshInfoIdx = Globals::rendererVK.addMeshInfos(m_meshInfos);
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

    m_baseMaterialInfoIdx = Globals::rendererVK.addMaterialInfos(m_materialInfos);
}

uint32 ObjectContainer::addMeshInstance(uint32 meshIdx)
{
    const uint32 meshInstanceIdx = m_numMeshInstancesForInfo[meshIdx]++;
    assert(meshInstanceIdx < USHRT_MAX && "Too many mesh instances for this mesh type");
    return meshInstanceIdx;
}

void ObjectContainer::updateRenderTransform(RenderNode& renderNode)
{
    updateRenderTransforms(renderNode.m_nodeIdx, (uint32)renderNode.m_numNodes);
}

void ObjectContainer::updateAllRenderTransforms()
{
    updateRenderTransforms(0, (uint32)m_renderNodes.size());
}

void ObjectContainer::updateRenderTransforms(uint32 startIdx, uint32 numNodes)
{
    static thread_local std::vector<Transform> worldSpaceNodes;
    worldSpaceNodes.resize(numNodes);

    for (uint32 i = 0; i < numNodes; ++i)
    {
        const RendererVKLayout::LocalSpaceNode& node = m_renderNodes[startIdx + i];
        const Transform parentNode = (node.parentOffset != 0) ? worldSpaceNodes[i - node.parentOffset] : Transform();
        Transform& nodeWS          = worldSpaceNodes[i];
        nodeWS.pos   = parentNode.pos + parentNode.quat * (node.transform.pos * parentNode.scale);
        nodeWS.quat  = parentNode.quat * node.transform.quat;
        nodeWS.scale = parentNode.scale * node.transform.scale;
        if (node.meshInfoIdx != USHRT_MAX)
        {
            RendererVKLayout::MeshInstance& meshInstance = m_meshInstancesForInfo[node.meshInfoIdx][node.meshInstanceIdx];
            meshInstance.transform.pos   = nodeWS.pos;
            meshInstance.transform.scale = nodeWS.scale;
            meshInstance.transform.quat  = nodeWS.quat;
            meshInstance.meshInfoIdx     = m_baseMeshInfoIdx + node.meshInfoIdx;
            meshInstance.materialInfoIdx = m_baseMaterialInfoIdx + m_materialIdxForMeshIdx[node.meshInfoIdx];
        }
    }
}

RenderNode ObjectContainer::addNodes(const std::vector<RendererVKLayout::LocalSpaceNode>& nodes, glm::vec3 pos, float scale, glm::quat quat)
{
    const size_t startIdx = m_renderNodes.size();
    const size_t numNodes = nodes.size();
    m_renderNodes.resize(startIdx + numNodes);
    memcpy(&m_renderNodes[startIdx], nodes.data(), numNodes * sizeof(nodes[0]));

    m_renderNodes[startIdx].transform.pos = pos;
    m_renderNodes[startIdx].transform.scale = scale;
    m_renderNodes[startIdx].transform.quat = quat;
    for (size_t i = startIdx; i < startIdx + numNodes; ++i)
    {
        RendererVKLayout::LocalSpaceNode& node = m_renderNodes[i];
        if (node.meshInfoIdx != USHRT_MAX)
            node.meshInstanceIdx = addMeshInstance(node.meshInfoIdx);
    }

    return RenderNode(this, (uint32)startIdx, (uint16)numNodes);
}

