export module RendererVK;

import Core;

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
export class FreeFlyCameraController;
export class ObjectContainer;

export class RendererVK final
{
public:

    RendererVK();
    ~RendererVK();
    RendererVK(const RendererVK&) = delete;

    bool initialize(Window& window, bool enableValidationLayers);
    void update(double deltaSec, const FreeFlyCameraController& camera);
    void render();

    uint32 getNumMeshInstances() const { return m_instanceCounter; }
    uint32 getNumMeshTypes() const { return m_meshInfoCounter; }
    uint32 getNumMaterials() const { return m_materialInfoCounter; }
    uint32 getCurrentFrameIndex() const { return m_swapChain.getCurrentFrameIndex(); }

private:

    void recordCommandBuffers();
    void setHaveToRecordCommandBuffers();

    friend class ObjectContainer;
    void addObjectContainer(ObjectContainer* pObjectContainer);
    uint32 addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos);
    uint32 addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos);

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

    uint32 m_instanceCounter = 0;
    uint32 m_meshInfoCounter = 0;
    uint32 m_materialInfoCounter = 0;

    Buffer m_materialInfoBuffer;
    struct PerFrameData
    {
        bool updated = false;
        Buffer uniformBuffer;
        RendererVKLayout::Ubo* mappedUniformBuffer = nullptr;
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