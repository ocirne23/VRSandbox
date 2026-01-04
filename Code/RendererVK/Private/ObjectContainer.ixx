export module RendererVK:ObjectContainer;

import Core;
import Core.glm;
import Core.Sphere;

import :Layout;
import :Buffer;
import :Texture;

export class SceneData;
export class TextureData;
export class MeshData;
export class MaterialData;
export class NodeData;
export class RenderNode;
export struct Transform;
       
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

    bool initialize(const SceneData& sceneData);
    bool isValid() const { return !m_nodeInfos.empty(); }

    NodeSpawnIdx getSpawnIdxForPath(const std::string& nodePath) const;
    RenderNode spawnNodeForPath(const std::string& nodePath, const Transform& transform);
    RenderNode spawnRootNode(const Transform& transform);
    RenderNode spawnNodeForIdx(NodeSpawnIdx idx, const Transform& transform);
    void getRootTransformForIdx(NodeSpawnIdx idx, Transform& transform);

    // TODO RenderNode cleanup

private:

    void initializeTextures(const std::vector<TextureData>& textureData);
    void initializeMeshes(const std::vector<MeshData>& meshData);
    void initializeMaterials(const std::vector<MaterialData>& materialData);
    void initializeNodes(const NodeData& nodeData);

private:

    struct NodeInfo
    {
        uint16 meshInfoIdx = UINT16_MAX;
        uint16 materialInfoIdx = UINT16_MAX;
    };

    struct NodeMeshRange
    {
        uint16 startIdx = UINT16_MAX;
        uint16 numNodes = UINT16_MAX;
    };

    std::vector<NodeInfo> m_nodeInfos;
    std::vector<NodeMeshRange> m_nodeMeshRanges;
    std::unordered_map<std::string, uint16> m_nodePathIdxLookup;

    std::vector<uint16> m_materialIdxForMeshIdx;
    std::vector<RendererVKLayout::MeshInstanceOffset> m_meshInstanceOffsets;
    std::vector<Sphere> m_meshInstanceBounds;
    std::vector<Sphere> m_boundsForMeshIdx;

    uint32 m_baseMeshInstanceOffsetsIdx = 0;
    uint16 m_baseMeshInfoIdx = 0;
    uint16 m_baseMaterialInfoIdx = 0;

    uint16 m_baseTextureIdx = 0;
    uint16 m_numTextures = 0;

    std::vector<std::string> m_meshNames;
    std::vector<std::string> m_materialNames;
};