module RendererVK:ObjectContainer;

import Core.Transform;

import File.SceneData;
import File.NodeData;
import :Renderer;
import :MeshDataManager;
import :RenderNode;
import :TextureManager;

constexpr char NODE_PATH_SEPARATOR = '/';
constexpr char NODE_CHILD_SEPARATOR = ':';

bool ObjectContainer::initialize(const SceneData& sceneData)
{
    if (!sceneData.isValid())
        return false;
    Globals::rendererVK.addObjectContainer(this);
    initializeTextures(sceneData.getTextures());
    initializeMaterials(sceneData.getMaterials());
    initializeMeshes(sceneData.getMeshes());
    initializeNodes(sceneData.getRootNode());
    return true;
}

ObjectContainer::~ObjectContainer()
{
    // TODO: cleanup
    // Globals::rendererVK.removeObjectContainer(this);
}

void ObjectContainer::initializeTextures(const std::vector<TextureData>& textureData)
{
    m_baseTextureIdx = Globals::textureManager.upload(textureData, true);
    m_numTextures = (uint16)textureData.size();
}

void ObjectContainer::initializeMeshes(const std::vector<MeshData>& meshDataList)
{
    const size_t numMeshes = meshDataList.size();

    std::vector<RendererVKLayout::MeshInfo> meshInfos;
    meshInfos.reserve(numMeshes);

    MeshDataManager& meshDataManager = Globals::meshDataManager;
    for (const MeshData& meshData : meshDataList)
    {
        RendererVKLayout::MeshInfo& meshInfo = meshInfos.emplace_back();
        m_meshNames.push_back(meshData.getName());
        m_materialIdxForMeshIdx.push_back((uint16)meshData.getMaterialIndex());

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

        Sphere sphereBounds;
        const AABB bounds = meshData.getAABB();
        sphereBounds.pos = bounds.getCenter();
        sphereBounds.radius = bounds.getRadius();
        m_boundsForMeshIdx.push_back(sphereBounds);

        std::vector<uint32> indices;
        meshData.getIndices(indices);
        meshInfo.indexCount   = (uint32)indices.size();
        meshInfo.vertexOffset = (int32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(RendererVKLayout::MeshVertex)) / sizeof(RendererVKLayout::MeshVertex));
        meshInfo.firstIndex   = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(RendererVKLayout::MeshIndex)) / sizeof(RendererVKLayout::MeshIndex));
        meshInfo.radius       = sphereBounds.radius;
        meshInfo.center       = sphereBounds.pos;
        meshInfo.firstInstance = 0;
    }

    m_baseMeshInfoIdx = (uint16)Globals::rendererVK.addMeshInfos(meshInfos);
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
    std::vector<RendererVKLayout::MaterialInfo> materialInfos;
    materialInfos.reserve(numMaterials);
    m_materialNames.reserve(numMaterials);

    for (const MaterialData& materialData : materialDataList)
    {
        RendererVKLayout::MaterialInfo& material = materialInfos.emplace_back();

        material.baseColor     = materialData.getBaseColor();
        material.roughness     = materialData.getRoughnessFactor();
        material.specularColor = materialData.getSpecularColor();
        material.metalness     = materialData.getMetalnessFactor();
        material.emissiveColor = materialData.getEmissiveColor() * materialData.getEmissiveIntensity();
        const uint32 diffuseTexIdx = materialData.getDiffuseTexIdx();
        if (diffuseTexIdx == UINT32_MAX || diffuseTexIdx >= UINT16_MAX)
            material.diffuseTexIdx = 0; // todo fallback texture
        else
            material.diffuseTexIdx = (uint16)diffuseTexIdx;

        const uint32 normalTexIdx = materialData.getNormalTexIdx();
        if (normalTexIdx == UINT32_MAX || normalTexIdx >= UINT16_MAX)
            material.normalTexIdx = 0; // todo fallback texture
        else
            material.normalTexIdx = (uint16)normalTexIdx;

        m_materialNames.push_back(materialData.getName());
    }

    m_baseMaterialInfoIdx = (uint16)Globals::rendererVK.addMaterialInfos(materialInfos);
}

void ObjectContainer::initializeNodes(const NodeData& nodeData)
{
    std::list<std::pair<const aiNode*, int>> nodeDataParentIdxStack;
    std::vector<Transform> localSpaceNodes;

    nodeDataParentIdxStack.push_back({ nodeData.getAiNode(), 0 });

    struct LocalSpaceNode
    {
        Transform transform;
        uint16 meshInfoIdx = UINT16_MAX;
        uint16 materialInfoIdx = UINT16_MAX;
        uint16 numChildren = 0;
        uint16 parentOffset = 0;
        std::string path;
    };

    std::vector<LocalSpaceNode> initialStateNodes;

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
        (void)(nonUniformScaleAmount);

        const uint32 numChildren = pAiNode->mNumChildren;
        const uint32 nodeIdx = (uint32)initialStateNodes.size();

        LocalSpaceNode& node = initialStateNodes.emplace_back();
        node.transform.pos = glm::vec3(pos.x, pos.y, pos.z);
        node.transform.scale = scale.x;
        node.transform.quat = glm::quat(rot.w, rot.x, rot.y, rot.z);

        node.numChildren = (uint16)numChildren;
        node.parentOffset = (uint16)(nodeIdx - parentIdx);
        node.path = isRoot ? pAiNode->mName.C_Str() : initialStateNodes[parentIdx].path + NODE_PATH_SEPARATOR + pAiNode->mName.C_Str();

        if (pAiNode->mNumMeshes > 0)
        {
            node.meshInfoIdx = (uint16)pAiNode->mMeshes[0];
            node.materialInfoIdx = m_materialIdxForMeshIdx[node.meshInfoIdx];

            // If the node has more than 1 mesh, add the remaining meshes as children of the current node
            if (pAiNode->mNumMeshes > 1)
                node.numChildren += (uint16)(pAiNode->mNumMeshes - 1);

            for (uint32 i = 1; i < pAiNode->mNumMeshes; ++i)
            {
                const uint32 childNodeIdx = (uint32)initialStateNodes.size();
                LocalSpaceNode& childNode = initialStateNodes.emplace_back();
                childNode.transform.pos = glm::vec3(0.0f);
                childNode.transform.scale = 1.0f;
                childNode.transform.quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childNode.parentOffset = (uint16)(childNodeIdx - nodeIdx);
                childNode.numChildren = 0;
                childNode.meshInfoIdx = (uint16)pAiNode->mMeshes[i];
                childNode.materialInfoIdx = m_materialIdxForMeshIdx[childNode.meshInfoIdx];
                childNode.path = initialStateNodes[nodeIdx].path + NODE_CHILD_SEPARATOR + std::to_string(i);
            }
        }
        else
        {
            node.meshInfoIdx = USHRT_MAX;
        }

        for (uint32 i = 0; i < numChildren; ++i)
        {
            nodeDataParentIdxStack.push_front({ pAiNode->mChildren[i], nodeIdx });
        }
    }

    std::vector<Transform> worldSpaceNodes;
    const size_t numNodes = initialStateNodes.size();
    worldSpaceNodes.resize(numNodes);

    m_nodeMeshRanges.resize(numNodes);

    for (uint32 i = 0; i < numNodes; ++i)
    {
        const LocalSpaceNode& node = initialStateNodes[i];
        const Transform parentTransform = (node.parentOffset != 0) ? worldSpaceNodes[i - node.parentOffset] : Transform();

        Transform& nodeWS = worldSpaceNodes[i];
        nodeWS.pos = parentTransform.pos + parentTransform.quat * (node.transform.pos * parentTransform.scale);
        nodeWS.quat = parentTransform.quat * node.transform.quat;
        nodeWS.scale = parentTransform.scale * node.transform.scale;

        if (node.meshInfoIdx != UINT16_MAX)
        {
            Sphere meshBounds = m_boundsForMeshIdx[node.meshInfoIdx];
            meshBounds.pos *= nodeWS.scale;
            meshBounds.radius *= nodeWS.scale;
            meshBounds.pos = meshBounds.pos * nodeWS.quat;
            meshBounds.pos += nodeWS.pos;
            m_meshInstanceBounds.emplace_back(meshBounds);

            if (m_nodeMeshRanges[i].startIdx == UINT16_MAX)
            {
                m_nodeMeshRanges[i].startIdx = (uint16)m_meshInstanceOffsets.size();
                m_nodeMeshRanges[i].numNodes = 1;
            }
            uint32 parentIdx = (node.parentOffset != 0) ? i - node.parentOffset : UINT16_MAX;
            while (parentIdx != UINT16_MAX)
            {
                if (m_nodeMeshRanges[parentIdx].startIdx == UINT16_MAX)
                {
                    m_nodeMeshRanges[parentIdx].startIdx = (uint16)m_meshInstanceOffsets.size();
                    m_nodeMeshRanges[parentIdx].numNodes = 0;
                }
                m_nodeMeshRanges[parentIdx].numNodes++;
                Sphere& parentBounds = m_meshInstanceBounds[parentIdx];
                parentBounds.combineSphere(meshBounds);

                parentIdx = (initialStateNodes[parentIdx].parentOffset != 0) ? parentIdx - initialStateNodes[parentIdx].parentOffset : UINT16_MAX;
            }

            m_nodePathIdxLookup.emplace(node.path, (uint16)i);
            m_nodeInfos.emplace_back((uint16)node.meshInfoIdx, node.materialInfoIdx);
            m_meshInstanceOffsets.emplace_back(nodeWS);
        }
        else if (i == 0)
        {
            m_nodePathIdxLookup.emplace(node.path, (uint16)i);
        }
    }

    m_baseMeshInstanceOffsetsIdx = Globals::rendererVK.addMeshInstanceOffsets(m_meshInstanceOffsets);
}

RenderNode ObjectContainer::spawnNodeForIdx(NodeSpawnIdx idx, const Transform& transform)
{
    const NodeMeshRange& range = m_nodeMeshRanges[idx];

    RenderNode node;
    node.m_transformIdx = Globals::rendererVK.addRenderNodeTransform(transform);
    node.m_bounds = m_meshInstanceBounds[idx];
    node.m_meshInstances.resize(range.numNodes);
    for (uint32 i = 0; i < range.numNodes; ++i)
    {
        node.m_meshInstances[i].renderNodeIdx = node.m_transformIdx;
        node.m_meshInstances[i].instanceOffsetIdx = m_baseMeshInstanceOffsetsIdx + range.startIdx + i;
        node.m_meshInstances[i].meshIdx = m_baseMeshInfoIdx + m_nodeInfos[range.startIdx + i].meshInfoIdx;
        node.m_meshInstances[i].materialIdx = m_baseMaterialInfoIdx + m_nodeInfos[range.startIdx + i].materialInfoIdx;
    }

    std::map<uint16, uint16> instancesPerMesh;
    for (uint32 i = 0; i < range.numNodes; ++i)
    {
        instancesPerMesh[node.m_meshInstances[i].meshIdx] += 1;
    }
    node.m_numInstancesPerMesh.reserve(instancesPerMesh.size());
    for (auto& pair : instancesPerMesh)
    {
        node.m_numInstancesPerMesh.emplace_back(pair);
    }

    return node;
}

void ObjectContainer::getRootTransformForIdx(NodeSpawnIdx idx, Transform& transform)
{
    transform = m_meshInstanceOffsets[m_nodeMeshRanges[idx].startIdx].transform;
}

NodeSpawnIdx ObjectContainer::getSpawnIdxForPath(const std::string& nodePath) const
{
    auto it = m_nodePathIdxLookup.find(nodePath);
    if (it == m_nodePathIdxLookup.end())
        return NodeSpawnIdx_INVALID;
    else
        return NodeSpawnIdx(it->second);
}

RenderNode ObjectContainer::spawnRootNode(const Transform& transform)
{
    return spawnNodeForIdx(NodeSpawnIdx_ROOT, transform);
}

RenderNode ObjectContainer::spawnNodeForPath(const std::string& nodePath, const Transform& transform)
{
    auto it = m_nodePathIdxLookup.find(nodePath);
    if (it == m_nodePathIdxLookup.end())
    {
        assert(false && "Could not find rendernode for path, spawning root");
        return spawnRootNode(transform);
    }
    else
    {
        return spawnNodeForIdx(NodeSpawnIdx(it->second), transform);
    }
}
