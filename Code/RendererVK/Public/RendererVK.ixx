export module RendererVK;

import Core;

import RendererVK.Instance;
import RendererVK.Device;
import RendererVK.Surface;
import RendererVK.SwapChain;
import RendererVK.RenderPass;
import RendererVK.Framebuffers;
import RendererVK.GraphicsPipeline;
import RendererVK.ComputePipeline;
import RendererVK.CommandBuffer;
import RendererVK.Buffer;
import RendererVK.Texture;
import RendererVK.Sampler;
import RendererVK.StagingManager;
import RendererVK.MeshDataManager;

namespace RendererVKLayout
{
    export struct Ubo;
    export struct MeshInfo;
    export struct MaterialInfo;
    export struct MeshInstance;
}

export class MeshInstance;
export class Window;
export class MeshData;
export class FreeFlyCameraController;
export class ObjectContainer;

export class RendererVK final
{
public:

    constexpr static uint32 NUM_FRAMES_IN_FLIGHT = 2;

    RendererVK();
    ~RendererVK();
    RendererVK(const RendererVK&) = delete;

    bool initialize(Window& window, bool enableValidationLayers);
    void update(double deltaSec, const FreeFlyCameraController& camera);
    void render();

    void recordCommandBuffers();

    const char* getDebugText();
    uint32 getNumMeshInstances() const { return m_instanceCounter; }
    uint32 getNumMeshTypes() const { return m_meshInfoCounter; }
    uint32 getNumMaterials() const { return m_materialInfoCounter; }

private:


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
    GraphicsPipeline m_graphicsPipeline;
    ComputePipeline m_computePipeline;
    Texture m_colorTex;
    Texture m_normalTex;
    Texture m_rmhTex;
    Sampler m_sampler;
    StagingManager m_stagingManager;

    friend class ObjectContainer;
    MeshDataManager m_meshDataManager;

    std::vector<ObjectContainer*> m_objectContainers;

    uint32 m_instanceCounter = 0;
    uint32 m_meshInfoCounter = 0;
    uint32 m_materialInfoCounter = 0;

    Buffer m_materialInfoBuffer;
    struct PerFrameData
    {
        bool updated = false;

        Buffer uniformBuffer;

        Buffer indirectCommandBuffer;
        Buffer instanceDataBuffer;
        Buffer instanceIdxBuffer;

        Buffer indirectDispatchBuffer;
        Buffer computeMeshInfoBuffer;

        vk::DispatchIndirectCommand* mappedDispatchBuffer = nullptr;
        RendererVKLayout::Ubo* mappedUniformBuffer = nullptr;
        RendererVKLayout::MeshInfo* mappedMeshInfo = nullptr;
        RendererVKLayout::MaterialInfo* mappedMaterialInfo = nullptr;
        RendererVKLayout::MeshInstance* mappedMeshInstances = nullptr;
    };
    std::array<PerFrameData, NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    RendererVK rendererVK;
#pragma warning(default: 4075)
}