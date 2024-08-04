export module RendererVK;

import Core;
import Core.glm;

import RendererVK.VK;
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
    export struct MeshInstance;
}
export class Mesh;

export class MeshInstance;
export class Window;
export class MeshData;

export class RendererVK final
{
public:

    constexpr static uint32 NUM_FRAMES_IN_FLIGHT = 2;

    RendererVK();
    ~RendererVK();
    RendererVK(const RendererVK&) = delete;

    bool initialize(Window& window, bool enableValidationLayers);
    void update(double deltaSec, const glm::mat4& mvpMatrix, std::span<MeshInstance> instances);
    void render();
    void updateMeshSet(std::vector<Mesh>& meshData);

    const char* getDebugText();

private:

    void recordCommandBuffers();

private:

    Instance m_instance;
    Device m_device;
    Surface m_surface;
    SwapChain m_swapChain;
    RenderPass m_renderPass;
    Framebuffers m_framebuffers;
    GraphicsPipeline m_graphicsPipeline;
    ComputePipeline m_computePipeline;
    Texture m_texture;
    Sampler m_sampler;
    StagingManager m_stagingManager;

    friend class Mesh;
    MeshDataManager m_meshDataManager;

    std::vector<Mesh>* m_pMeshSet = nullptr;

    struct PerFrameData
    {
        bool updated = false;

        Buffer uniformBuffer;
        Buffer indirectCommandBuffer;
        Buffer instanceDataBuffer;

        Buffer indirectDispatchBuffer;
        Buffer computeMeshInfoBuffer;
        Buffer computeMeshInstanceBuffer;

        vk::DispatchIndirectCommand* mappedDispatchBuffer = nullptr;
        RendererVKLayout::Ubo* mappedUniformBuffer = nullptr;
        RendererVKLayout::MeshInfo* mappedMeshInfo = nullptr;
        RendererVKLayout::MeshInstance* mappedMeshInstances = nullptr;
    };
    std::array<PerFrameData, NUM_FRAMES_IN_FLIGHT> m_perFrameData;

    uint32 m_instanceCounter = 0;
};

export namespace VK
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    RendererVK g_renderer;
#pragma warning(default: 4075)
}