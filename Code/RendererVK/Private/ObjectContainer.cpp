module RendererVK;

import Core;
import Core.Transform;
import Core.Skeleton;
import Core.glm;

import File;

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

    const Skeleton* pSkeleton = sceneData.getSkeleton();
    m_isSkinned = pSkeleton != nullptr;
    m_numSkeletonBones = pSkeleton ? pSkeleton->numBones() : 0;

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

        // Capture skinning source data. The bind-pose base geometry above stays uploaded (and is what the
        // skinning compute reads); spawnSkinnedNode allocates per-instance output regions that get drawn.
        if (meshData.isSkinned())
        {
            const uint32 numVerts = meshData.getNumVertices();
            const glm::uvec4* pBoneIndices = meshData.getBoneIndices();
            const glm::vec4* pBoneWeights = meshData.getBoneWeights();
            std::vector<RendererVKLayout::SkinningVertex> skinVerts(numVerts);
            for (uint32 i = 0; i < numVerts; ++i)
            {
                skinVerts[i].boneIndices = pBoneIndices[i];
                skinVerts[i].boneWeights = pBoneWeights[i];
            }
            const uint32 skinVertexOffset = (uint32)(meshDataManager.uploadSkinningData(
                skinVerts.data(), skinVerts.size() * sizeof(RendererVKLayout::SkinningVertex)) / sizeof(RendererVKLayout::SkinningVertex));

            const uint16 materialLocalIdx = (uint16)meshData.getMaterialIndex();
            SkinnedMeshSource src{
                .baseVertexOffset = (uint32)meshInfo.vertexOffset,
                .skinVertexOffset = skinVertexOffset,
                .vertexCount = numVerts,
                .indexCount = meshInfo.indexCount,
                .firstIndex = meshInfo.firstIndex,
                .materialLocalIdx = materialLocalIdx,
                .pipelineIdx = (uint16)temp.pipelineAlphaForMaterialIdx[materialLocalIdx].first,
                .alphaMode = (uint16)temp.pipelineAlphaForMaterialIdx[materialLocalIdx].second,
                .bounds = sphereBounds,
            };
            m_skinnedMeshes.push_back(src);
        }
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
    // Skinned meshes are deformed per-frame on the GPU; their BLAS would be stale, so they're excluded
    // from ray tracing (GI / RTAO / RT shadows) for now by forcing every material to instance mask 0.
    const bool excludeFromRayTracing = sceneData.getSkeleton() != nullptr;
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

        if (excludeFromRayTracing)
            material.flags |= RendererVKLayout::MATERIAL_FLAG_NO_RAYTRACING;

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

RenderNode ObjectContainer::spawnSkinnedNode(const Transform& transform)
{
    assert(m_isSkinned && !m_skinnedMeshes.empty() && "spawnSkinnedNode on a non-skinned container");

    Renderer& renderer = Globals::rendererVK;
    MeshDataManager& meshDataManager = Globals::meshDataManager;

    RenderNode node;
    node.m_transformIdx = renderer.addRenderNodeTransform(transform);

    // One shared identity per-mesh offset: skinned vertices are produced in armature/model space already,
    // so the per-instance offset is identity and only the node (root) transform places them in the world.
    if (m_skinnedIdentityOffsetIdx == UINT32_MAX)
        m_skinnedIdentityOffsetIdx = renderer.addMeshInstanceOffsets({ RendererVKLayout::MeshInstanceOffset{} });

    // One palette per node, shared by all its skinned meshes (same skeleton).
    const uint32 paletteHandle = renderer.allocateSkinningPalette(m_numSkeletonBones);
    node.m_skinnedPaletteHandle = paletteHandle;

    // Reserve a unique output vertex region per skinned mesh and register a MeshInfo pointing at it.
    std::vector<RendererVKLayout::MeshInfo> meshInfos;
    std::vector<uint32> outVertexOffsets;
    meshInfos.reserve(m_skinnedMeshes.size());
    outVertexOffsets.reserve(m_skinnedMeshes.size());
    Sphere combinedBounds{ glm::vec3(0.0f), 0.0f };
    for (const SkinnedMeshSource& src : m_skinnedMeshes)
    {
        const uint32 outVertexOffset = (uint32)(meshDataManager.reserveVertexData(
            (size_t)src.vertexCount * sizeof(RendererVKLayout::MeshVertex)) / sizeof(RendererVKLayout::MeshVertex));
        outVertexOffsets.push_back(outVertexOffset);

        RendererVKLayout::MeshInfo& mi = meshInfos.emplace_back();
        mi.indexCount = src.indexCount;
        mi.firstIndex = src.firstIndex;
        mi.vertexOffset = (int32)outVertexOffset;
        mi.center = src.bounds.pos;
        // Animation deforms the mesh beyond its bind-pose bounds; inflate so it isn't frustum-culled early.
        mi.radius = src.bounds.radius * 2.0f;
        mi.firstInstance = 0;
        Sphere inflated(src.bounds.pos, src.bounds.radius * 2.0f);
        combinedBounds.combineSphere(inflated);
    }
    const uint32 baseMeshIdx = renderer.addMeshInfos(meshInfos);

    node.m_bounds = combinedBounds;
    node.m_meshInstances.resize(m_skinnedMeshes.size());
    std::map<uint16, uint16> instancesPerMesh;
    for (uint32 k = 0; k < (uint32)m_skinnedMeshes.size(); ++k)
    {
        const SkinnedMeshSource& src = m_skinnedMeshes[k];
        const uint16 meshIdx = (uint16)(baseMeshIdx + k);

        renderer.addSkinnedInstance(src.baseVertexOffset, src.skinVertexOffset, outVertexOffsets[k], src.vertexCount, paletteHandle);

        RendererVKLayout::InMeshInstance& inst = node.m_meshInstances[k];
        inst.renderNodeIdx = node.m_transformIdx;
        inst.instanceOffsetIdx = m_skinnedIdentityOffsetIdx;
        inst.meshIdx = meshIdx;
        inst.materialIdx = m_baseMaterialInfoIdx + src.materialLocalIdx;
        inst.pipelineIndex = src.pipelineIdx;
        inst.alphaMode = src.alphaMode;
        instancesPerMesh[meshIdx] += 1;
    }
    node.m_numInstancesPerMesh.reserve(instancesPerMesh.size());
    for (auto& pair : instancesPerMesh)
        node.m_numInstancesPerMesh.emplace_back(pair);

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
