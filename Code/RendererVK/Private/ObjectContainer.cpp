module;

#include <meshoptimizer/meshoptimizer.h> // LOD chain generation for meshes without authored LODs

module RendererVK;

import Core;
import Core.Transform;
import Core.glm;
import Core.Log;

import Animation;
import File;

import :Renderer;
import :MeshDataManager;
import :RenderNode;
import :TextureManager;

constexpr char NODE_PATH_SEPARATOR = '/';
constexpr char NODE_CHILD_SEPARATOR = ':';

// Meshes named "Col_*" (or sitting on a "Col_*" node) are collision-only proxies: the physics
// CollisionSource picks them up, rendering skips them (their data is still uploaded so mesh indices
// stay aligned with the scene, they just never get instanced).
static constexpr std::string_view COLLISION_MESH_PREFIX = "Col_";

// Meshes (or nodes) named "LodN_*" form authored LOD chains: "Lod0_house"/"Lod1_house"/... group under
// the logical name "house" (a plain "house" mesh may also serve as level 0). Levels above 0 are never
// instanced directly; Renderer::selectMeshLods redirects instances to them by projected size, and a
// "Lod0_*" node is also reachable by its logical name in path lookups.
static bool parseLodName(std::string_view name, uint32& outLevel, std::string_view& outLogicalName)
{
    if (name.size() < 5 || (name[0] != 'L' && name[0] != 'l') || (name[1] != 'O' && name[1] != 'o') || (name[2] != 'D' && name[2] != 'd'))
        return false;
    size_t i = 3;
    if (name[i] < '0' || name[i] > '9')
        return false;
    uint32 level = 0;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9')
        level = level * 10 + (name[i++] - '0');
    if (i >= name.size() || name[i] != '_' || i + 1 >= name.size())
        return false;
    outLevel = level;
    outLogicalName = name.substr(i + 1);
    return true;
}

struct ObjectContainer::TempInitData
{
	std::vector<std::pair<RendererVKLayout::EPipelineIndex, RendererVKLayout::EAlphaMode>> pipelineAlphaForMaterialIdx;
    std::vector<uint16> materialIdxForMeshIdx;
    std::vector<uint16> textureIdxForMaterialTex;
    std::vector<bool> meshIsLodVariant;      // level > 0 member of an authored chain: never instanced
    std::vector<uint32> lodGroupForMeshIdx;  // per local mesh: Renderer LOD group (UINT32_MAX = none)
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
    std::vector<RendererVKLayout::SkinnedMeshSource> skinnedSources; // registered with the renderer after the loop

    // Authored LOD chains: pre-scan the mesh names so chain membership is known before upload (chain
    // members are excluded from meshopt generation, level > 0 members from instancing).
    struct AuthoredLods { uint16 levels[RendererVKLayout::MAX_MESH_LODS]; }; // local mesh idx per level
    std::unordered_map<std::string, AuthoredLods> authoredLods;
    std::unordered_map<std::string, uint16> meshIdxByName;
    std::vector<bool> inAuthoredChain(numMeshes, false);
    temp.meshIsLodVariant.assign(numMeshes, false);
    temp.lodGroupForMeshIdx.assign(numMeshes, UINT32_MAX);
    for (uint32 meshIdx = 0; meshIdx < numMeshes; meshIdx++)
        meshIdxByName.try_emplace(sceneData.getMesh(meshIdx)->getName(), (uint16)meshIdx);
    for (uint32 meshIdx = 0; meshIdx < numMeshes; meshIdx++)
    {
        uint32 level;
        std::string_view logicalName;
        if (!parseLodName(sceneData.getMesh(meshIdx)->getName(), level, logicalName) || level >= RendererVKLayout::MAX_MESH_LODS)
            continue;
        auto [it, inserted] = authoredLods.try_emplace(std::string(logicalName));
        if (inserted)
            std::fill(std::begin(it->second.levels), std::end(it->second.levels), (uint16)UINT16_MAX);
        it->second.levels[level] = (uint16)meshIdx;
        if (level > 0)
            temp.meshIsLodVariant[meshIdx] = true;
    }
    for (auto& [logicalName, lods] : authoredLods)
    {
        // A plain "house" mesh serves as level 0 when no "Lod0_house" exists.
        if (lods.levels[0] == UINT16_MAX)
            if (auto it = meshIdxByName.find(logicalName); it != meshIdxByName.end())
                lods.levels[0] = it->second;
        for (uint16 idx : lods.levels)
            if (idx != UINT16_MAX)
                inAuthoredChain[idx] = true;
    }

    // meshopt-generated LOD chains: extra index-only MeshInfos over the same vertex ranges, appended
    // after the source meshes (filled during the upload loop below).
    struct GeneratedChain { uint16 baseMeshIdx; uint16 firstExtraIdx; uint8 numLevels; };
    std::vector<RendererVKLayout::MeshInfo> generatedLodInfos;
    std::vector<GeneratedChain> generatedChains;
    const MeshLodParams& lodParams = Globals::rendererVK.getLodParams();

    const Skeleton* pSkeleton = sceneData.getSkeleton();
    m_isSkinned = pSkeleton != nullptr;
    m_numSkeletonBones = pSkeleton ? pSkeleton->numBones() : 0;
    // ISceneData is typically freed right after the container loads, so keep our own copy of the skeleton
    // (an AnimatorComponent retargets its clips against it at spawn time).
    if (pSkeleton)
        m_skeleton = std::make_unique<Skeleton>(*pSkeleton);

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
            skinnedSources.push_back(RendererVKLayout::SkinnedMeshSource{
                .baseVertexOffset = (uint32)meshInfo.vertexOffset,
                .skinVertexOffset = skinVertexOffset,
                .vertexCount = numVerts,
                .indexCount = meshInfo.indexCount,
                .firstIndex = meshInfo.firstIndex,
                .materialLocalIdx = materialLocalIdx,
                .pipelineIdx = (uint16)temp.pipelineAlphaForMaterialIdx[materialLocalIdx].first,
                .alphaMode = (uint16)temp.pipelineAlphaForMaterialIdx[materialLocalIdx].second,
                .bounds = sphereBounds,
            });
        }

        // meshopt-generated LOD chain: quarter the triangles per level over the same vertices (matches
        // the selector's per-level halving of the projected diameter, since density scales with area).
        const bool isColMesh = std::string_view(m_meshNames[meshIdx]).starts_with(COLLISION_MESH_PREFIX);
        if (lodParams.generate && !meshData.isSkinned() && !isColMesh && !inAuthoredChain[meshIdx]
            && numIndices >= (uint32)lodParams.minIndices)
        {
            std::vector<uint32> lodIndices(numIndices);
            uint32 prevIndexCount = numIndices;
            uint8 numLevels = 0;
            for (int level = 1; level <= lodParams.generateLevels && level < (int)RendererVKLayout::MAX_MESH_LODS; ++level)
            {
                const size_t targetIndexCount = std::max<size_t>(12, ((size_t)numIndices >> (2 * level)) / 3 * 3);
                if (targetIndexCount >= prevIndexCount)
                    break;
                const float targetError = 0.01f * (float)(1u << (level - 1));
                const size_t resultCount = meshopt_simplify(lodIndices.data(), pIndices, numIndices,
                    &vertices[0].position.x, vertices.size(), sizeof(RendererVKLayout::MeshVertex),
                    targetIndexCount, targetError, 0, nullptr);
                if (resultCount < 3 || resultCount >= (size_t)((float)prevIndexCount * 0.9f))
                    break; // simplification stalled; deeper levels won't gain anything
                RendererVKLayout::MeshInfo lodInfo = meshInfo; // same vertices + bounds
                lodInfo.indexCount = (uint32)resultCount;
                lodInfo.firstIndex = (uint32)(meshDataManager.uploadIndexData(lodIndices.data(), resultCount * sizeof(RendererVKLayout::MeshIndex)) / sizeof(RendererVKLayout::MeshIndex));
                if (numLevels == 0)
                    generatedChains.push_back(GeneratedChain{ (uint16)meshIdx, (uint16)generatedLodInfos.size(), 0 });
                generatedLodInfos.push_back(lodInfo);
                generatedChains.back().numLevels = ++numLevels;
                prevIndexCount = (uint32)resultCount;
            }
        }
    }

    const uint32 numSourceMeshes = (uint32)meshInfos.size();
    meshInfos.insert(meshInfos.end(), generatedLodInfos.begin(), generatedLodInfos.end());

    m_baseMeshInfoIdx = (uint16)Globals::rendererVK.addMeshInfos(meshInfos);

    // Register the LOD chains now that global mesh indices are known; the base mesh's NodeInfo picks
    // the group up in initializeNodes.
    for (const GeneratedChain& chain : generatedChains)
    {
        MeshLodGroup group;
        group.numLods = (uint8)(chain.numLevels + 1);
        group.meshIdx[0] = (uint16)(m_baseMeshInfoIdx + chain.baseMeshIdx);
        for (uint8 k = 0; k < chain.numLevels; ++k)
            group.meshIdx[k + 1] = (uint16)(m_baseMeshInfoIdx + numSourceMeshes + chain.firstExtraIdx + k);
        group.center = m_boundsForMeshIdx[chain.baseMeshIdx].pos;
        group.radius = m_boundsForMeshIdx[chain.baseMeshIdx].radius;
        temp.lodGroupForMeshIdx[chain.baseMeshIdx] = Globals::rendererVK.addMeshLodGroup(group);
    }
    for (const auto& [logicalName, lods] : authoredLods)
    {
        if (lods.levels[0] == UINT16_MAX)
        {
            Log::warning(std::format("Scene: LOD chain '{}' in '{}' has no level 0 ('Lod0_{}' or plain '{}'); ignoring", logicalName, m_filePath, logicalName, logicalName));
            continue;
        }
        MeshLodGroup group;
        for (uint16 localIdx : lods.levels) // ascending level order; missing levels compact down
            if (localIdx != UINT16_MAX)
                group.meshIdx[group.numLods++] = (uint16)(m_baseMeshInfoIdx + localIdx);
        if (group.numLods < 2)
            continue; // a lone Lod0_ mesh is just a regular mesh
        group.center = m_boundsForMeshIdx[lods.levels[0]].pos;
        group.radius = m_boundsForMeshIdx[lods.levels[0]].radius;
        temp.lodGroupForMeshIdx[lods.levels[0]] = Globals::rendererVK.addMeshLodGroup(group);
    }
    if (!skinnedSources.empty())
    {
        m_numSkinnedMeshes = (uint32)skinnedSources.size();
        m_baseSkinnedMeshIdx = Globals::rendererVK.addSkinnedMeshSources(skinnedSources);
        // One shared identity per-mesh offset for all skinned instances of this container: skinned
        // vertices are produced in armature/model space already, so only the node (root) transform places
        // them in the world. Registered at load time so spawnSkinnedNode stays re-record-free.
        m_skinnedIdentityOffsetIdx = Globals::rendererVK.addMeshInstanceOffsets({ RendererVKLayout::MeshInstanceOffset{} });
    }
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

    // Loose texture files are referenced relative to the scene file; record its folder on each texture so
    // the loader resolves them next to the .fbx/.glb first, then falls back to the asset root.
    const std::string rootFolder = std::filesystem::path(sceneData.getFilePath()).parent_path().string();
    for (uint32 texIdx = 0; texIdx < sceneData.getNumTextures(); texIdx++)
        const_cast<ITextureData*>(sceneData.getTexture(texIdx))->setRootFolder(rootFolder);

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
		// Trust the resolved alpha mode: glTF reports it explicitly (and per spec an OPAQUE material ignores
		// its base-color alpha), while getAlphaMode() already infers Blend from opacity for formats that
		// don't. Don't re-derive blend from opacity here, or explicit-OPAQUE glTF materials render transparent.
		const bool isBlend     = alphaMode == IMaterialData::EAlphaMode::Blend;

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

        // BC5 normal maps store only X/Y, so the shader must reconstruct Z. Flag it from the actual
        // uploaded format so RGB(A) normal maps keep using their stored Z (works for either convention).
        if (material.normalTexIdx != UINT32_MAX)
        {
            const vk::Format normalFormat = Globals::textureManager.getTexture(material.normalTexIdx).getFormat();
            if (normalFormat == vk::Format::eBc5UnormBlock || normalFormat == vk::Format::eBc5SnormBlock)
                material.flags |= RendererVKLayout::MATERIAL_FLAG_BC5_NORMAL;
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
        if (nonUniformScaleAmount > 0.001f)
        {
			std::string msg = std::format("Scene: node '{}' in '{}' has non-uniform scale ({}, {}, {}), which is not supported and may cause visual artifacts", pStackNode->getName(), m_filePath, scale.x, scale.y, scale.z);
            Log::warning(msg);
        }
        //assert(nonUniformScaleAmount < 0.0001f && "Non-uniform scaling is not supported");
        //(void)(nonUniformScaleAmount);

        const uint32 numChildren = pStackNode->getNumChildren();
        const uint32 nodeIdx = (uint32)initialStateNodes.size();

        LocalSpaceNode& node = initialStateNodes.emplace_back();
        node.transform.pos = glm::vec3(pos.x, pos.y, pos.z);
        node.transform.scale = scale.x;
        node.transform.quat = glm::quat(rot.w, rot.x, rot.y, rot.z);

        node.numChildren = (uint16)numChildren;
        node.parentOffset = (uint16)(nodeIdx - parentIdx);
        node.path = isRoot ? pStackNode->getName() : initialStateNodes[parentIdx].path + NODE_PATH_SEPARATOR + pStackNode->getName();

        std::vector<uint32> visibleMeshes;
        const bool nodeIsCollision = std::string_view(pStackNode->getName()).starts_with(COLLISION_MESH_PREFIX);
        uint32 nodeLodLevel = 0;
        std::string_view nodeLodLogicalName;
        const bool nodeIsLodVariant = parseLodName(pStackNode->getName(), nodeLodLevel, nodeLodLogicalName) && nodeLodLevel > 0;
        for (uint32 i = 0; i < pStackNode->getNumMeshes(); ++i)
        {
            const uint32 meshIdx = pStackNode->getMeshIndex(i);
            if (nodeIsCollision || std::string_view(m_meshNames[meshIdx]).starts_with(COLLISION_MESH_PREFIX))
                continue; // collision-only proxy mesh, never rendered
            if (nodeIsLodVariant || temp.meshIsLodVariant[meshIdx])
                continue; // LOD level > 0: only reachable through its chain's level-0 instance
            visibleMeshes.push_back(meshIdx);
        }

        if (!visibleMeshes.empty())
        {
            node.meshInfoIdx = (uint16)visibleMeshes[0];
            node.materialInfoIdx = temp.materialIdxForMeshIdx[node.meshInfoIdx];

            // If the node has more than 1 mesh, add the remaining meshes as children of the current node
            if (visibleMeshes.size() > 1)
                node.numChildren += (uint16)(visibleMeshes.size() - 1);

            for (uint32 i = 1; i < (uint32)visibleMeshes.size(); ++i)
            {
                const uint32 childNodeIdx = (uint32)initialStateNodes.size();
                LocalSpaceNode& childNode = initialStateNodes.emplace_back();
                childNode.transform.pos = glm::vec3(0.0f);
                childNode.transform.scale = 1.0f;
                childNode.transform.quat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                childNode.parentOffset = (uint16)(childNodeIdx - nodeIdx);
                childNode.numChildren = 0;
                childNode.meshInfoIdx = (uint16)visibleMeshes[i];
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
    m_rebasedOffsetBaseForIdx.assign(numNodes, UINT32_MAX);

    // A "Lod0_X" leaf is also reachable as "X" (spawnables/prefabs reference the logical name).
    auto registerPathWithLodAlias = [this](const std::string& path, uint16 nodeIdx)
    {
        m_nodePathIdxLookup.emplace(path, nodeIdx);
        const size_t leafStart = path.find_last_of(NODE_PATH_SEPARATOR) + 1; // npos + 1 == 0: whole path is the leaf
        uint32 lodLevel;
        std::string_view logicalName;
        if (parseLodName(std::string_view(path).substr(leafStart), lodLevel, logicalName) && lodLevel == 0)
            m_nodePathIdxLookup.try_emplace(path.substr(0, leafStart) + std::string(logicalName), nodeIdx);
    };

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

            // Ranges index the mesh-instance arrays (m_nodeInfos / m_meshInstanceOffsets), which hold ONLY
            // mesh-bearing nodes in DFS order. A subtree's mesh nodes are contiguous there, so a range covers
            // exactly them; a node-index range would instead pull in interleaved mesh-less nodes (whose
            // meshInfoIdx is UINT16_MAX), producing a bogus mesh index.
            const uint16 meshInstanceIdx = (uint16)m_meshInstanceOffsets.size();
            if (m_nodeMeshRanges[i].startIdx == UINT16_MAX)
            {
                m_nodeMeshRanges[i].startIdx = meshInstanceIdx;
                m_nodeMeshRanges[i].numNodes = 1;
            }
            uint32 parentIdx = (node.parentOffset != 0) ? i - node.parentOffset : UINT16_MAX;
            while (parentIdx != UINT16_MAX)
            {
                if (m_nodeMeshRanges[parentIdx].startIdx == UINT16_MAX)
                {
                    m_nodeMeshRanges[parentIdx].startIdx = meshInstanceIdx;
                    m_nodeMeshRanges[parentIdx].numNodes = 0;
                }
                m_nodeMeshRanges[parentIdx].numNodes++;
                Sphere& parentBounds = m_nodeBounds[parentIdx];
                parentBounds.combineSphere(meshBounds);

                parentIdx = (initialStateNodes[parentIdx].parentOffset != 0) ? parentIdx - initialStateNodes[parentIdx].parentOffset : UINT16_MAX;
            }

            registerPathWithLodAlias(node.path, (uint16)i);
            m_meshInstanceOffsets.emplace_back(nodeWS);
            // Parallel to m_meshInstanceOffsets: one NodeInfo per mesh instance (not per node).
            m_nodeInfos.emplace_back(NodeInfo{
                .meshInfoIdx = node.meshInfoIdx,
                .materialInfoIdx = node.materialInfoIdx,
                .pipelineIdx = node.materialInfoIdx != UINT16_MAX ? (uint16)temp.pipelineAlphaForMaterialIdx[node.materialInfoIdx].first : UINT16_MAX,
                .alphaMode = node.materialInfoIdx != UINT16_MAX ? (uint16)temp.pipelineAlphaForMaterialIdx[node.materialInfoIdx].second : UINT16_MAX,
                .lodGroupIdx = temp.lodGroupForMeshIdx[node.meshInfoIdx]
            });
        }
        else if (i == 0)
        {
            registerPathWithLodAlias(node.path, (uint16)i);
        }
        m_nodeBounds.emplace_back(meshBounds);
    }

    m_nodeRootTransforms = std::move(worldSpaceNodes);
    m_baseMeshInstanceOffsetsIdx = Globals::rendererVK.addMeshInstanceOffsets(m_meshInstanceOffsets);
}

RenderNode ObjectContainer::spawnNodeForIdx(NodeSpawnIdx idx, const Transform& transform)
{
    assert(idx < m_nodeMeshRanges.size() && "Invalid NodeSpawnIdx");
    const NodeMeshRange& range = m_nodeMeshRanges[idx];

    RenderNode node;
    node.m_transformIdx = Globals::rendererVK.addRenderNodeTransform(transform);

    // The baked per-instance offsets are relative to the container ROOT, so a sub-node spawned by path
    // carries its whole ancestor chain (e.g. a Blender group's transform). Rebase those offsets (and the
    // node bounds) into the node's own space so `transform` places the node's pivot itself, not
    // pivot * ancestors. ROOT spawns want the container-relative offsets, so they're left untouched.
    const bool rebase = idx != NodeSpawnIdx_ROOT;
    const Transform invNode = rebase ? m_nodeRootTransforms[idx].inverse() : Transform();

    node.m_bounds = m_nodeBounds[idx];
    if (rebase)
    {
        node.m_bounds.pos = invNode.transformPoint(node.m_bounds.pos);
        node.m_bounds.radius *= invNode.scale;
    }

    // Rebased offsets are uploaded once per idx and shared by every instance (see m_rebasedOffsetBaseForIdx).
    uint32 rebasedOffsetBase = 0;
    if (rebase)
    {
        uint32& cachedBase = m_rebasedOffsetBaseForIdx[idx];
        if (cachedBase == UINT32_MAX)
        {
            std::vector<RendererVKLayout::MeshInstanceOffset> rebasedOffsets;
            rebasedOffsets.reserve(range.numNodes);
            for (uint32 i = 0; i < range.numNodes; ++i)
                rebasedOffsets.emplace_back(invNode * m_meshInstanceOffsets[range.startIdx + i].transform);
            cachedBase = Globals::rendererVK.addMeshInstanceOffsets(rebasedOffsets);
        }
        rebasedOffsetBase = cachedBase;
    }

    node.m_meshInstances.resize(range.numNodes);
    for (uint32 i = 0; i < range.numNodes; ++i)
    {
        const uint32 meshInstanceIdx = range.startIdx + i; // index into the mesh-only NodeInfo / offset arrays
        const NodeInfo& nodeInfo = m_nodeInfos[meshInstanceIdx];

        node.m_meshInstances[i].renderNodeIdx = node.m_transformIdx;
        node.m_meshInstances[i].instanceOffsetIdx = rebase ? (rebasedOffsetBase + i) : (m_baseMeshInstanceOffsetsIdx + meshInstanceIdx);

        node.m_meshInstances[i].meshIdx = m_baseMeshInfoIdx + nodeInfo.meshInfoIdx;
        node.m_meshInstances[i].materialIdx = m_baseMaterialInfoIdx + nodeInfo.materialInfoIdx;
		node.m_meshInstances[i].pipelineIndex = nodeInfo.pipelineIdx;
		node.m_meshInstances[i].alphaMode = nodeInfo.alphaMode;
        if (nodeInfo.lodGroupIdx != UINT32_MAX)
            node.m_lodInstances.emplace_back(i, nodeInfo.lodGroupIdx);
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
    assert(m_isSkinned && m_numSkinnedMeshes > 0 && "spawnSkinnedNode on a non-skinned container");

    Renderer& renderer = Globals::rendererVK;

    RenderNode node;
    node.m_transformIdx = renderer.addRenderNodeTransform(transform);

    // A parked bundle from a destroyed skinned node of this container has everything still valid and
    // identical (MeshInfos, output vertex regions, palette region, skinning jobs), so reuse is free.
    uint32 bundleHandle = renderer.acquireSkinnedBundle(m_baseSkinnedMeshIdx);
    if (bundleHandle == UINT32_MAX)
    {
        MeshDataManager& meshDataManager = Globals::meshDataManager;

        // One palette per node, shared by all its skinned meshes (same skeleton).
        const uint32 paletteHandle = renderer.allocateSkinningPalette(m_numSkeletonBones);

        // Reserve a unique output vertex region per skinned mesh and register a MeshInfo pointing at it.
        std::vector<RendererVKLayout::MeshInfo> meshInfos;
        std::vector<uint32> outVertexOffsets;
        meshInfos.reserve(m_numSkinnedMeshes);
        outVertexOffsets.reserve(m_numSkinnedMeshes);
        for (uint32 k = 0; k < m_numSkinnedMeshes; ++k)
        {
            const RendererVKLayout::SkinnedMeshSource& src = renderer.getSkinnedMeshSource(m_baseSkinnedMeshIdx + k);
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
        }
        const uint32 baseMeshIdx = renderer.addMeshInfos(meshInfos);

        uint32 firstJob = 0;
        for (uint32 k = 0; k < m_numSkinnedMeshes; ++k)
        {
            const RendererVKLayout::SkinnedMeshSource& src = renderer.getSkinnedMeshSource(m_baseSkinnedMeshIdx + k);
            const uint16 meshIdx = (uint16)(baseMeshIdx + k);
            assert(meshIdx < UINT16_MAX);
            const uint32 jobHandle = renderer.addSkinnedInstance(src.baseVertexOffset, src.skinVertexOffset,
                outVertexOffsets[k], src.vertexCount, paletteHandle, meshIdx, src.firstIndex, src.indexCount);
            if (k == 0)
                firstJob = jobHandle;
        }
        bundleHandle = renderer.registerSkinnedBundle(Renderer::SkinnedInstanceBundle{
            .sourceKey = m_baseSkinnedMeshIdx, .baseMeshIdx = baseMeshIdx, .paletteHandle = paletteHandle,
            .firstJob = firstJob, .numMeshes = m_numSkinnedMeshes });
    }

    const Renderer::SkinnedInstanceBundle& bundle = renderer.getSkinnedBundle(bundleHandle);
    node.m_skinnedBundleHandle = bundleHandle;
    node.m_skinnedPaletteHandle = bundle.paletteHandle;

    Sphere combinedBounds{ glm::vec3(0.0f), 0.0f };
    node.m_meshInstances.resize(m_numSkinnedMeshes);
    std::map<uint16, uint16> instancesPerMesh;
    for (uint32 k = 0; k < m_numSkinnedMeshes; ++k)
    {
        const RendererVKLayout::SkinnedMeshSource& src = renderer.getSkinnedMeshSource(m_baseSkinnedMeshIdx + k);
        // Matches the registered MeshInfo's inflated radius (animation deforms beyond bind-pose bounds).
        Sphere inflated(src.bounds.pos, src.bounds.radius * 2.0f);
        combinedBounds.combineSphere(inflated);

        RendererVKLayout::InMeshInstance& inst = node.m_meshInstances[k];
        inst.renderNodeIdx = node.m_transformIdx;
        inst.instanceOffsetIdx = m_skinnedIdentityOffsetIdx;
        inst.meshIdx = (uint16)(bundle.baseMeshIdx + k);
        inst.materialIdx = m_baseMaterialInfoIdx + src.materialLocalIdx;
        inst.pipelineIndex = src.pipelineIdx;
        inst.alphaMode = src.alphaMode;
        instancesPerMesh[inst.meshIdx] += 1;
    }
    node.m_bounds = combinedBounds;
    node.m_numInstancesPerMesh.reserve(instancesPerMesh.size());
    for (auto& pair : instancesPerMesh)
    {
        assert(pair.first != UINT16_MAX);
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

std::vector<std::string> ObjectContainer::getNodePaths() const
{
    std::vector<std::string> paths;
    paths.reserve(m_nodePathIdxLookup.size());
    for (const auto& kv : m_nodePathIdxLookup)
        paths.push_back(kv.first);
    return paths;
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
