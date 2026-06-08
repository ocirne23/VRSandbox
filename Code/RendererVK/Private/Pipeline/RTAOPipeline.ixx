export module RendererVK:RTAOPipeline;

import Core;
import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :DescriptorSet;
import :Layout;

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
    void initialize(uint32 fullWidth, uint32 fullHeight);
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
        uint32 frameIndex; // free-running frame counter (RNG / temporal rotation)
    };
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, const RecordParams& params);

    // Final denoised AO (half-res); sampled by the forward pass with getAOSampler() (linear upsample).
    vk::ImageView getAOView(uint32 frameIdx) const { return m_final.view[frameIdx]; }
    vk::Sampler   getAOSampler() const { return m_aoSampler; }
    uint32 getWidth() const  { return m_width; }
    uint32 getHeight() const { return m_height; }

private:
    struct ImageSet
    {
        std::array<vk::Image, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> image;
        std::array<vk::DeviceMemory, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> memory;
        std::array<vk::ImageView, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> view;
        std::array<bool, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> initialized{};
    };

    void buildTraceLayout(ComputePipelineLayout& layout);
    void buildTemporalLayout(ComputePipelineLayout& layout);
    void buildSpatialLayout(ComputePipelineLayout& layout);
    void createImageSet(ImageSet& set);
    void destroyImageSet(ImageSet& set);
    void transitionToGeneral(vk::CommandBuffer cmd, ImageSet& set, uint32 frameIdx, bool forWrite);

    ComputePipeline m_tracePipeline;
    ComputePipeline m_temporalPipeline;
    ComputePipeline m_spatialPipeline;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_traceSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_temporalSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_spatialSets;

    uint32 m_width = 0;  // half-res AO dimensions
    uint32 m_height = 0;
    ImageSet m_raw;
    ImageSet m_accum;
    ImageSet m_final;
    vk::Sampler m_aoSampler;
};
