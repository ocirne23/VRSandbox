export module RendererVK:TaaPipeline;

import Core;
import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :DescriptorSet;
import :Layout;

// Temporal anti-aliasing resolve. A single full-resolution compute pass per frame reprojects last frame's
// resolved colour (history) into the current frame via the depth buffer + u_prevMvp, clamps it to the local
// neighbourhood, and blends. The output doubles as the displayed image and next frame's history, so the
// resolved images are ping-ponged per frame-in-flight (like the RTAO accumulation / GI grid). Images stay in
// GENERAL (storage written, then sampled by the composite pass), matching the RTAO convention.
export class TaaPipeline final
{
public:
    void initialize(uint32 width, uint32 height);
    void recreateImages(uint32 width, uint32 height);
    void reloadShaders();

    struct RecordParams
    {
        Buffer& ubo;
        vk::ImageView currentColorView;      // this frame's scene colour (SHADER_READ_ONLY)
        vk::Sampler   currentColorSampler;
        vk::ImageView gbufferDepthView;      // this frame's camera depth (SHADER_READ_ONLY)
        vk::ImageView prevGbufferDepthView;  // last frame's camera depth (SHADER_READ_ONLY)
        vk::Sampler   gbufferSampler;
        float feedback;                      // history weight; 0 disables accumulation
    };
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, const RecordParams& params);

    // Resolved colour for this frame; sampled by the composite pass with getSampler() (linear).
    vk::ImageView getResolvedView(uint32 frameIdx) const { return m_resolved.view[frameIdx]; }
    vk::Sampler   getSampler() const { return m_sampler; }
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

    void buildLayout(ComputePipelineLayout& layout);
    void createImageSet(ImageSet& set);
    void destroyImageSet(ImageSet& set);
    void transitionToGeneral(vk::CommandBuffer cmd, ImageSet& set, uint32 frameIdx, bool forWrite);

    ComputePipeline m_pipeline;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_sets;

    uint32 m_width = 0;
    uint32 m_height = 0;
    ImageSet m_resolved;
    vk::Sampler m_sampler;
};
