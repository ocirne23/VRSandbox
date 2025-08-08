export module RendererVK;

import Core;

import RendererVK.Transform;
import RendererVK.Layout;
import RendererVK.Instance;
import RendererVK.Device;
import RendererVK.Surface;
import RendererVK.SwapChain;
import RendererVK.RenderPass;
import RendererVK.Framebuffers;
import RendererVK.CommandBuffer;
import RendererVK.Buffer;

import RendererVK.StagingManager;
import RendererVK.MeshDataManager;

import RendererVK.IndirectCullComputePipeline;
import RendererVK.StaticMeshGraphicsPipeline;

export class MeshInstance;
export class Window;
export class MeshData;
export class ObjectContainer;
export struct Camera;

export class RendererVK final
{
public:

    RendererVK();
    ~RendererVK();
    RendererVK(const RendererVK&) = delete;

    bool initialize(Window& window, bool enableValidationLayers);
    void update(double deltaSec, const Camera& camera);
    void render();

    uint32 getNumMeshInstances() const { return m_meshInstanceCounter; }
    uint32 getNumRenderNodes() const { return (uint32)m_renderNodeTransforms.size(); }
    uint32 getNumMeshTypes() const { return m_meshInfoCounter; }
    uint32 getNumMaterials() const { return m_materialInfoCounter; }
    uint32 getCurrentFrameIndex() const { return m_swapChain.getCurrentFrameIndex(); }

private:

    void recordCommandBuffers();
    void setHaveToRecordCommandBuffers();

    friend class ObjectContainer;
    void addObjectContainer(ObjectContainer* pObjectContainer);

    uint32 addRenderNodeTransform(const Transform& transform);
    void addMeshInstances(const std::vector<RendererVKLayout::InMeshInstance>& meshInstances);
    uint32 addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos);
    uint32 addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos);
    uint32 addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets);

    friend class RenderNode;
    inline Transform& getRenderNodeTransform(uint32 idx) { return m_renderNodeTransforms[idx]; }

private:

    Instance m_instance;
    Device m_device;
    Surface m_surface;
    SwapChain m_swapChain;
    RenderPass m_renderPass;
    Framebuffers m_framebuffers;
    StagingManager m_stagingManager;

    friend class ObjectContainer;
    MeshDataManager m_meshDataManager;

    IndirectCullComputePipeline m_indirectCullComputePipeline;
    StaticMeshGraphicsPipeline m_staticMeshGraphicsPipeline;

    std::vector<ObjectContainer*> m_objectContainers;
    std::vector<Transform> m_renderNodeTransforms;
    std::vector<std::vector<RendererVKLayout::InMeshInstance>> m_meshInstancesForInfo;

    uint32 m_meshInfoCounter = 0;
    uint32 m_materialInfoCounter = 0;
    uint32 m_instanceOffsetCounter = 0;
    uint32 m_meshInstanceCounter = 0;

    Buffer m_meshInfosBuffer;
    Buffer m_materialInfosBuffer;
    Buffer m_instanceOffsetsBuffer;

    struct PerFrameData
    {
        bool updated = false;
        Buffer ubo;
        Buffer inRenderNodeTransformsBuffer;
        Buffer inMeshInstancesBuffer;
        Buffer inFirstInstancesBuffer;

        RendererVKLayout::Ubo* mappedUniformBuffer = nullptr;
        std::span<RendererVKLayout::RenderNodeTransform> mappedRenderNodeTransforms;
        std::span<RendererVKLayout::InMeshInstance> mappedMeshInstances;
        std::span<uint32> mappedFirstInstances;
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    RendererVK rendererVK;
#pragma warning(default: 4075)
}