export module RendererVK:VolumetricFogPipeline;

import Core;
import :VK;
import :Allocator;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :GraphicsPipeline;
import :DescriptorSet;
import :Layout;

export class RenderPass;

// Froxel-based volumetric fog (camera-frustum-aligned 3D grid, VOL_FROXEL_* texels, exponential Z slices):
//   1. scatter   : per froxel, media density (global height fog + noise + local fog volumes) and in-scattered
//                  light (sun via TLAS ray / cascade tap, GI probe ambient, light-grid lights), temporally
//                  blended against last frame's reprojected grid (ping-ponged across frames in flight).
//   2. integrate : front-to-back accumulation along Z -> in-scatter + transmittance at every slice.
//   3. apply     : fullscreen blend in the scene-color pass (before TAA), sampling the integrated volume at
//                  each pixel's G-buffer depth: out = inScatter + sceneColor * transmittance.
// The grid resolution is fixed (independent of the window size), so no swapchain-recreate handling is needed.
export class VolumetricFogPipeline final
{
public:
    VolumetricFogPipeline() = default;
    ~VolumetricFogPipeline();
    VolumetricFogPipeline(const VolumetricFogPipeline&) = delete;

    void initialize();
    // viewCount > 1 (VR): one apply descriptor set per eye so the per-eye inline draws don't clobber each other.
    void initializeApply(vk::RenderPass renderPass, uint32 viewCount = 1);
    void reloadShaders(vk::RenderPass renderPass);

    struct RecordParams
    {
        Buffer& ubo;
        Buffer& lightInfosBuffer;
        Buffer& lightGridsBuffer;
        Buffer& lightTableBuffer;
        Buffer& fogVolumesBuffer;
        Buffer& giGridDataBuffer;
        vk::ImageView shadowMapView;
        vk::Sampler   shadowMapSampler;
        vk::AccelerationStructureKHR tlas;
    };
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, const RecordParams& params);

    struct ApplyParams
    {
        Buffer& ubo;
        vk::ImageView gbufferDepthView;
        vk::Sampler   gbufferSampler;
    };
    // Records the fullscreen apply draw; the caller has begun a command buffer inside the scene-color
    // render pass and set the viewport/scissor. eye selects the per-eye depth/projection (0 = desktop/left).
    void recordApply(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 eye, const ApplyParams& params);

private:
    struct ImageSet
    {
        std::array<vk::Image, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> image;
        std::array<VmaAllocation, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> memory{};
        std::array<vk::ImageView, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> view;
    };

    void buildScatterLayout(ComputePipelineLayout& layout);
    void buildIntegrateLayout(ComputePipelineLayout& layout);
    void buildApplyLayout(GraphicsPipelineLayout& layout);
    void createImageSet(ImageSet& set);
    void destroyImageSet(ImageSet& set);

    ComputePipeline m_scatterPipeline;
    ComputePipeline m_integratePipeline;
    GraphicsPipeline m_applyPipeline;
    static constexpr uint32 MAX_VIEWS = 2;
    static uint32 applySlot(uint32 frameIdx, uint32 eye) { return frameIdx * MAX_VIEWS + eye; }
    uint32 m_applyViewCount = 1;

    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_scatterSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_integrateSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT * MAX_VIEWS> m_applySets;

    ImageSet m_scatter;    // per-froxel in-scatter/extinction; previous frame's image is the temporal history
    ImageSet m_integrated; // accumulated in-scatter + transmittance per slice
    vk::Sampler m_sampler;
};
