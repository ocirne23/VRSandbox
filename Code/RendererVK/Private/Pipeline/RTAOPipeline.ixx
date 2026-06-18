export module RendererVK:RTAOPipeline;

import Core;
import :VK;
import :Allocator;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :DescriptorSet;
import :Layout;
import :Settings;

// Screen-space ray-traced ambient occlusion with a temporal-reprojection + spatial denoise.
// Three half-resolution compute passes per frame, all R16F (images kept in GENERAL, no layout churn):
//   1. trace    : ray-query the GI TLAS from the G-buffer -> raw (noisy) AO.
//   2. temporal : reproject last frame's accumulated AO via prevMvp, reject on disocclusion, blend ->
//                 accumulated AO (this frame's becomes next frame's history; ping-ponged like the GI grid).
//   3. spatial  : depth/normal-aware bilateral blur of the accumulated AO -> final AO (sampled by the
//                 forward pass and upsampled with a linear sampler).
export class RTAOPipeline final
{
public:
    RTAOPipeline() = default;
    ~RTAOPipeline();
    RTAOPipeline(const RTAOPipeline&) = delete;

    // viewCount > 1 (VR) keeps a separate AO image set + history per eye (record() takes the eye index).
    void initialize(const RTAOParams* pParams, uint32 fullWidth, uint32 fullHeight, uint32 viewCount = 1);
    void recreateImages(uint32 fullWidth, uint32 fullHeight);
    void reloadShaders();

    struct RecordParams
    {
        Buffer& ubo;
        vk::ImageView gbufferNormalView;     // this frame
        vk::ImageView gbufferDepthView;      // this frame
        vk::ImageView prevGbufferDepthView;  // previous frame (disocclusion)
        vk::Sampler   gbufferSampler;
        vk::AccelerationStructureKHR tlas;
    };
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 eye, const RecordParams& params);

    // Final denoised AO (half-res) for an eye; sampled by the forward pass with getAOSampler() (linear upsample).
    vk::ImageView getAOView(uint32 frameIdx, uint32 eye) const { return m_final.view[slot(frameIdx, eye)]; }
    vk::Sampler   getAOSampler() const { return m_aoSampler; }
    uint32 getWidth() const  { return m_width; }
    uint32 getHeight() const { return m_height; }

private:
    static constexpr uint32 MAX_VIEWS = 2;
    static constexpr uint32 SLOTS = RendererVKLayout::NUM_FRAMES_IN_FLIGHT * MAX_VIEWS;
    static uint32 slot(uint32 frameIdx, uint32 eye) { return frameIdx * MAX_VIEWS + eye; }

    struct ImageSet
    {
        std::array<vk::Image, SLOTS> image{};
        std::array<VmaAllocation, SLOTS> memory{};
        std::array<vk::ImageView, SLOTS> view{};
        std::array<bool, SLOTS> initialized{};
    };

    void buildTraceLayout(ComputePipelineLayout& layout);
    void buildTemporalLayout(ComputePipelineLayout& layout);
    void buildSpatialLayout(ComputePipelineLayout& layout);
    void createImageSet(ImageSet& set);
    void destroyImageSet(ImageSet& set);
    void transitionToGeneral(vk::CommandBuffer cmd, ImageSet& set, uint32 slotIdx, bool forWrite);

    uint32 m_viewCount = 1;
    ComputePipeline m_tracePipeline;
    ComputePipeline m_temporalPipeline;
    ComputePipeline m_spatialPipeline;
    std::array<DescriptorSet, SLOTS> m_traceSets;
    std::array<DescriptorSet, SLOTS> m_temporalSets;
    std::array<DescriptorSet, SLOTS> m_spatialSets;

    const RTAOParams* m_pParams = nullptr;

    uint32 m_width = 0;  // half-res AO dimensions
    uint32 m_height = 0;
    ImageSet m_raw;
    ImageSet m_accum;
    ImageSet m_final;
    vk::Sampler m_aoSampler;
};
