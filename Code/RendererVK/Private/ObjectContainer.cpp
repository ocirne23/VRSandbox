module RendererVK;

import Core;
import Core.Transform;

import File.ISceneData;
import File.INodeData;
import File.IMeshData;
import File.IMaterialData;
import File.ITextureData;

import :Renderer;
import :MeshDataManager;
import :RenderNode;
import :TextureManager;

constexpr char NODE_PATH_SEPARATOR = '/';
constexpr char NODE_CHILD_SEPARATOR = ':';

struct ObjectContainer::TempInitData
{
	std::vector<std::pair<RendererVKLayout::EPipelineIndex, RendererVKLayout::EAlphaMode>> pipelineAlphaForMaterialIdx;
    std::vector<uint16> materialIdxForMeshIdx;
    std::vector<uint16> textureIdxForMaterialTex;
};

bool ObjectContainer::initialize(const ISceneData& sceneData, const MaterialOverrides* pOverrides)
{
    if (!sceneData.isValid())
        return false;
	m_filePath = sceneData.getFilePath();

    Globals::rendererVK.addObjectContainer(this);

    TempInitData temp;
    initializeMaterials(sceneData, temp, pOverrides);
    initializeMeshes(sceneData, temp);
    initializeNodes(sceneData, temp);
    return true;
}

ObjectContainer::~ObjectContainer()
{
    // TODO: cleanup
    // Globals::rendererVK.removeObjectContainer(this);
}

void ObjectContainer::initializeMeshes(const ISceneData& sceneData, TempInitData& temp)
{
    const size_t numMeshes = sceneData.getNumMeshes();

    std::vector<RendererVKLayout::MeshInfo> meshInfos;
    meshInfos.reserve(numMeshes);

    MeshDataManager& meshDataManager = Globals::meshDataManager;
	for (uint32 meshIdx = 0; meshIdx < numMeshes; meshIdx++)
	{
		const IMeshData& meshData = *sceneData.getMesh(meshIdx);
        RendererVKLayout::MeshInfo& meshInfo = meshInfos.emplace_back();
        m_meshNames.push_back(meshData.getName());
        temp.materialIdxForMeshIdx.push_back((uint16)meshData.getMaterialIndex());

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

		const uint32 numIndices = meshData.getNumIndices();
        const uint32* pIndices = meshData.getIndices();
        meshInfo.indexCount   = numIndices;
        meshInfo.vertexOffset = (int32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(RendererVKLayout::MeshVertex)) / sizeof(RendererVKLayout::MeshVertex));
        meshInfo.firstIndex   = (uint32)(meshDataManager.uploadIndexData(pIndices, numIndices * sizeof(RendererVKLayout::MeshIndex)) / sizeof(RendererVKLayout::MeshIndex));
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

void ObjectContainer::initializeMaterials(const ISceneData& sceneData, TempInitData& temp, const MaterialOverrides* pOverrides)
{
    const size_t numMaterials = sceneData.getNumMaterials();
    std::vector<RendererVKLayout::MaterialInfo> materialInfos;
	temp.textureIdxForMaterialTex.resize(sceneData.getNumTextures(), UINT16_MAX);
    materialInfos.reserve(numMaterials);
    m_materialNames.reserve(numMaterials);

    for (uint32 materialIdx = 0; materialIdx < numMaterials; materialIdx++)
    {
        const IMaterialData& materialData = *sceneData.getMaterial(materialIdx);
        RendererVKLayout::MaterialInfo& material = materialInfos.emplace_back();

        //material.baseColor     = materialData.getBaseColor();
        //material.roughness     = materialData.getRoughnessFactor();
        //material.specularColor = materialData.getSpecularColor();
        //material.metalness     = materialData.getMetalnessFactor();
        //material.emissiveColor = materialData.getEmissiveColor() * materialData.getEmissiveIntensity();
        material.diffuseTexIdx = RendererVKLayout::FALLBACK_DIFFUSE_TEX_IDX;
		material.normalTexIdx  = RendererVKLayout::FALLBACK_NORMAL_TEX_IDX;
        material.metalRoughnessTexIdx = UINT16_MAX;

		const IMaterialData::EAlphaMode alphaMode = materialData.getAlphaMode();
		const float opacity    = materialData.getOpacity();
		//const float opacity    = materialInfos.size() % 2 == 0 ? 0.5f : 1.0f; // testing blend mode by forcing every other material to be blended
		const bool isBlend     = alphaMode == IMaterialData::EAlphaMode::Blend || (alphaMode == IMaterialData::EAlphaMode::Opaque && opacity < 1.0f);

		if (isBlend)
		{
			material.alphaMode = (uint32)RendererVKLayout::EAlphaMode::Blend;
			material.opacity = opacity;
			temp.pipelineAlphaForMaterialIdx.emplace_back(RendererVKLayout::EPipelineIndex::LitTransparent, RendererVKLayout::EAlphaMode::Blend);
		}
		else if (alphaMode == IMaterialData::EAlphaMode::Mask)
        {
            material.alphaMode = (uint32)RendererVKLayout::EAlphaMode::Mask;
            material.opacity = materialData.getAlphaCutoff(); // cutoff stored in opacity for masked materials
			temp.pipelineAlphaForMaterialIdx.emplace_back(RendererVKLayout::EPipelineIndex::LitOpaque, RendererVKLayout::EAlphaMode::Mask);
        }
        else
        {
            material.alphaMode = (uint32)RendererVKLayout::EAlphaMode::Opaque;
            material.opacity = opacity;
			temp.pipelineAlphaForMaterialIdx.emplace_back(RendererVKLayout::EPipelineIndex::LitOpaque, RendererVKLayout::EAlphaMode::Opaque);
        }

        if (pOverrides)
        {
            temp.pipelineAlphaForMaterialIdx.back().first = pOverrides->pipelineIdx;
            if (pOverrides->excludeFromRayTracing)
                material.flags |= RendererVKLayout::MATERIAL_FLAG_NO_RAYTRACING;
            if (pOverrides->pipelineIdx == RendererVKLayout::EPipelineIndex::Sky)
                material.flags |= RendererVKLayout::MATERIAL_FLAG_SKY;
        }
        if (pOverrides && !pOverrides->useSceneTextures)
        {
            material.diffuseTexIdx = pOverrides->diffuseTexIdx;
            material.normalTexIdx = pOverrides->normalTexIdx;
            material.metalRoughnessTexIdx = pOverrides->metalRoughnessTexIdx;
        }
        else
        {
            const uint32 diffuseTexIdx = materialData.getDiffuseTexIdx();
            if (diffuseTexIdx != UINT32_MAX)
            {
                uint16& idx = temp.textureIdxForMaterialTex[diffuseTexIdx];
                if (idx == UINT16_MAX)
                    idx = Globals::textureManager.upload(*sceneData.getTexture(diffuseTexIdx), true, true);
                material.diffuseTexIdx = idx;
            }

            const uint32 normalTexIdx = materialData.getNormalTexIdx();
            if (normalTexIdx != UINT32_MAX)
            {
                uint16& idx = temp.textureIdxForMaterialTex[normalTexIdx];
                if (idx == UINT16_MAX)
                    idx = Globals::textureManager.upload(*sceneData.getTexture(normalTexIdx), true);
                material.normalTexIdx = idx;
            }

            const uint32 metalRoughnessTexIdx = materialData.getMetalRoughnessTexIdx();
            if (metalRoughnessTexIdx != UINT32_MAX)
            {
                uint16& idx = temp.textureIdxForMaterialTex[metalRoughnessTexIdx];
                if (idx == UINT16_MAX)
                    idx = Globals::textureManager.upload(*sceneData.getTexture(metalRoughnessTexIdx), false);
                material.metalRoughnessTexIdx = idx;
            }
        }

        m_materialNames.push_back(materialData.getName());
    }

    m_baseMaterialInfoIdx = (uint16)Globals::rendererVK.addMaterialInfos(materialInfos);
}

void ObjectContainer::initializeNodes(const ISceneData& sceneData, TempInitData& temp)
{
    std::list<std::pair<std::unique_ptr<INodeData>, int>> nodeDataParentIdxStack;
    std::vector<Transform> localSpaceNodes;

    nodeDataParentIdxStack.emplace_back(sceneData.getRootNode().clone(), 0);

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
		std::pair<std::unique_ptr<INodeData>, int>& front = nodeDataParentIdxStack.front();
        std::unique_ptr<INodeData> pStackNode = std::move(front.first);
		int parentIdx = front.second;
        nodeDataParentIdxStack.pop_front();

        bool isRoot = parentIdx == 0;

        glm::vec3 pos, scale;
        glm::quat rot;
        pStackNode->getTransform(pos, scale, rot);

        const float nonUniformScaleAmount = glm::max(glm::distance(scale.x, scale.y), glm::distance(scale.x, scale.z));
        assert(nonUniformScaleAmount < 0.0001f && "Non-uniform scaling is not supported");
        (void)(nonUniformScaleAmount);

        const uint32 numChildren = pStackNode->getNumChildren();
        const uint32 nodeIdx = (uint32)initialStateNodes.size();

        LocalSpaceNode& node = initialStateNodes.emplace_back();
        node.transform.pos = glm::vec3(pos.x, pos.y, pos.z);
        node.transform.scale = scale.x;
        node.transform.quat = glm::quat(rot.w, rot.x, rot.y, rot.z);

        node.numChildren = (uint16)numChildren;
        node.parentOffset = (uint16)(nodeIdx - parentIdx);
        node.path = isRoot ? pStackNode->getName() : initialStateNodes[parentIdx].path + NODE_PATH_SEPARATOR + pStackNode->getName();

        if (pStackNode->getNumMeshes() > 0)
        {
            node.meshInfoIdx = (uint16)pStackNode->getMeshIndex(0);
            node.materialInfoIdx = temp.materialIdxForMeshIdx[node.meshInfoIdx];

            // If the node has more than 1 mesh, add the remaining meshes as children of the current node
            if (pStackNode->getNumMeshes() > 1)
                node.numChildren += (uint16)(pStackNode->getNumMeshes() - 1);

            for (uint32 i = 1; i < pStackNode->getNumMeshes(); ++i)
            {
                const uint32 childNodeIdx = (uint32)initialStateNodes.size();
                LocalSpaceNode& childNode = initialStateNodes.emplace_back();
                childNode.transform.pos = glm::vec3(0.0f);
                childNode.transform.scale = 1.0f;
                childNode.transform.quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childNode.parentOffset = (uint16)(childNodeIdx - nodeIdx);
                childNode.numChildren = 0;
				childNode.meshInfoIdx = (uint16)pStackNode->getMeshIndex(i);
                childNode.materialInfoIdx = temp.materialIdxForMeshIdx[childNode.meshInfoIdx];
                childNode.path = initialStateNodes[nodeIdx].path + NODE_CHILD_SEPARATOR + std::to_string(i);
            }
        }
        else
        {
            node.meshInfoIdx = USHRT_MAX;
        }

        for (uint32 i = 0; i < numChildren; ++i)
        {
            nodeDataParentIdxStack.emplace_front(pStackNode->getChild(i), nodeIdx);
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

		Sphere meshBounds{ glm::vec3(0.0f), 0.0f };

        if (node.meshInfoIdx != UINT16_MAX)
        {
            meshBounds = m_boundsForMeshIdx[node.meshInfoIdx];
            meshBounds.pos *= nodeWS.scale;
            meshBounds.radius *= nodeWS.scale;
            meshBounds.pos = meshBounds.pos * nodeWS.quat;
            meshBounds.pos += nodeWS.pos;

            if (m_nodeMeshRanges[i].startIdx == UINT16_MAX)
            {
                m_nodeMeshRanges[i].startIdx = (uint16)m_nodeBounds.size();
                m_nodeMeshRanges[i].numNodes = 1;
            }
            uint32 parentIdx = (node.parentOffset != 0) ? i - node.parentOffset : UINT16_MAX;
            while (parentIdx != UINT16_MAX)
            {
                if (m_nodeMeshRanges[parentIdx].startIdx == UINT16_MAX)
                {
                    m_nodeMeshRanges[parentIdx].startIdx = (uint16)m_nodeBounds.size();
                    m_nodeMeshRanges[parentIdx].numNodes = 0;
                }
                m_nodeMeshRanges[parentIdx].numNodes++;
                Sphere& parentBounds = m_nodeBounds[parentIdx];
                parentBounds.combineSphere(meshBounds);

                parentIdx = (initialStateNodes[parentIdx].parentOffset != 0) ? parentIdx - initialStateNodes[parentIdx].parentOffset : UINT16_MAX;
            }

            m_nodePathIdxLookup.emplace(node.path, (uint16)i);
            m_meshInstanceOffsets.emplace_back(nodeWS);
        }
        else if (i == 0)
        {
            m_nodePathIdxLookup.emplace(node.path, (uint16)i);
        }
        m_nodeBounds.emplace_back(meshBounds);

        NodeInfo nodeInfo{
            .meshInfoIdx = node.meshInfoIdx,
            .materialInfoIdx = node.materialInfoIdx,
            .pipelineIdx = node.materialInfoIdx != UINT16_MAX ? (uint16)temp.pipelineAlphaForMaterialIdx[node.materialInfoIdx].first : UINT16_MAX,
            .alphaMode = node.materialInfoIdx != UINT16_MAX ? (uint16)temp.pipelineAlphaForMaterialIdx[node.materialInfoIdx].second : UINT16_MAX
        };
        m_nodeInfos.emplace_back(nodeInfo);
    }

    m_baseMeshInstanceOffsetsIdx = Globals::rendererVK.addMeshInstanceOffsets(m_meshInstanceOffsets);
}

RenderNode ObjectContainer::spawnNodeForIdx(NodeSpawnIdx idx, const Transform& transform)
{
    assert(idx < m_nodeMeshRanges.size() && "Invalid NodeSpawnIdx");
    const NodeMeshRange& range = m_nodeMeshRanges[idx];

    RenderNode node;
    node.m_transformIdx = Globals::rendererVK.addRenderNodeTransform(transform);
    node.m_bounds = m_nodeBounds[idx];
    node.m_meshInstances.resize(range.numNodes);
    for (uint32 i = 0; i < range.numNodes; ++i)
    {
        const NodeInfo& nodeInfo = m_nodeInfos[range.startIdx + i];

        node.m_meshInstances[i].renderNodeIdx = node.m_transformIdx;
        node.m_meshInstances[i].instanceOffsetIdx = m_baseMeshInstanceOffsetsIdx + nodeInfo.meshInfoIdx;

        node.m_meshInstances[i].meshIdx = m_baseMeshInfoIdx + nodeInfo.meshInfoIdx;
        node.m_meshInstances[i].materialIdx = m_baseMaterialInfoIdx + nodeInfo.materialInfoIdx;
		node.m_meshInstances[i].pipelineIndex = nodeInfo.pipelineIdx;
		node.m_meshInstances[i].alphaMode = nodeInfo.alphaMode;
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
