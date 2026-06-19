export module RendererVK.ObjectContainer;

import Core;
import Core.glm;
import Core.Sphere;
import Core.fwd;

import File.fwd;

import RendererVK.fwd;
import RendererVK.Layout;
import RendererVK.Buffer;
import RendererVK.Texture;

export enum NodeSpawnIdx : uint16
{
    NodeSpawnIdx_ROOT = 0,
    NodeSpawnIdx_INVALID = UINT16_MAX
};

export class ObjectContainer final
{
public:

    friend class RendererVK;
    friend class RenderNode;
    friend class IndirectCullComputePipeline;

    ObjectContainer() = default;
    ~ObjectContainer();
    ObjectContainer(const ObjectContainer&) = delete;
    ObjectContainer(const ObjectContainer&&) = delete;
    ObjectContainer& operator=(const ObjectContainer&) = delete;
    ObjectContainer& operator=(const ObjectContainer&&) = delete;

    // When passed to initialize(), every material is forced onto the given pipeline, and (unless
    // useSceneTextures is set) uses these texture indices instead of its own scene textures.
    struct MaterialOverrides
    {
        uint16 diffuseTexIdx = RendererVKLayout::FALLBACK_DIFFUSE_TEX_IDX;
        uint16 normalTexIdx = RendererVKLayout::FALLBACK_NORMAL_TEX_IDX;
        uint16 metalRoughnessTexIdx = UINT16_MAX;
        bool excludeFromRayTracing = false;
        bool useSceneTextures = true; // if false use the above texture indices instead of the material's own scene textures
        RendererVKLayout::EPipelineIndex pipelineIdx = RendererVKLayout::EPipelineIndex::UnlitOpaque;
    };

    bool initialize(const ISceneData& sceneData, const MaterialOverrides* pOverrides = nullptr);
    bool isValid() const { return !m_nodeInfos.empty(); }

    NodeSpawnIdx getSpawnIdxForPath(const std::string& nodePath) const;
    RenderNode spawnNodeForPath(const std::string& nodePath, const Transform& transform);
    RenderNode spawnRootNode(const Transform& transform);
    RenderNode spawnNodeForIdx(NodeSpawnIdx idx, const Transform& transform);
    void getRootTransformForIdx(NodeSpawnIdx idx, Transform& transform);

	const std::string& getFilePath() const { return m_filePath; }

    // TODO RenderNode cleanup

private:

    struct TempInitData;
    void initializeMeshes(const ISceneData& sceneData, TempInitData& temp);
    void initializeMaterials(const ISceneData& sceneData, TempInitData& temp, const MaterialOverrides* pOverrides);
    void initializeNodes(const ISceneData& sceneData, TempInitData& temp);

private:

    struct NodeInfo
    {
        uint16 meshInfoIdx = UINT16_MAX;
        uint16 materialInfoIdx = UINT16_MAX;
        uint16 pipelineIdx = 0;
        uint16 alphaMode = 0;
    };

    struct NodeMeshRange
    {
        uint16 startIdx = UINT16_MAX;
        uint16 numNodes = UINT16_MAX;
    };
    std::string m_filePath;

    std::vector<NodeInfo> m_nodeInfos;
    std::vector<NodeMeshRange> m_nodeMeshRanges;
    std::unordered_map<std::string, uint16> m_nodePathIdxLookup;

    std::vector<RendererVKLayout::MeshInstanceOffset> m_meshInstanceOffsets;
    std::vector<Sphere> m_nodeBounds;
    std::vector<Sphere> m_boundsForMeshIdx;

    uint32 m_baseMeshInstanceOffsetsIdx = 0;
    uint16 m_baseMeshInfoIdx = 0;
    uint16 m_baseMaterialInfoIdx = 0;

    std::vector<std::string> m_meshNames;
    std::vector<std::string> m_materialNames;
};