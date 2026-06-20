export module RendererVK:SkinningComputePipeline;

import Core;
import Core.glm;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :Layout;
import :DescriptorSet;

// GPU skinning: deforms each skinned mesh instance's base vertices into a per-instance output region of
// the shared vertex mega-buffer, using a bone-matrix palette. One dispatch per skinned instance (params
// passed as push constants). Output is in the standard MeshVertex format (model space), so the existing
// cull / DGC / G-buffer / shadow paths consume it unchanged.
export class SkinningComputePipeline final
{
public:
    SkinningComputePipeline();
    ~SkinningComputePipeline();
    SkinningComputePipeline(const SkinningComputePipeline&) = delete;

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        Buffer& vertexBuffer;   // 0 - shared MeshVertex mega-buffer (read base region, write output region)
        Buffer& skinningBuffer; // 1 - SkinningVertex influences
        std::span<const RendererVKLayout::SkinningPushConstants> jobs;
    };

    void initialize(uint32 maxPaletteEntries);
    void reloadShaders();
    // Copies the concatenated bone palettes for this frame into the mapped GPU palette buffer.
    void update(uint32 frameIdx, std::span<const glm::mat4> palettes);
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, RecordParams& params);
    void resizePaletteBuffer(uint32 maxPaletteEntries);

    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_computePipeline.getDescriptorSetLayout(); }

private:

    void buildComputeLayout(ComputePipelineLayout& layout);

    ComputePipeline m_computePipeline;
    uint32 m_maxPaletteEntries = 0;

    struct PerFrameData
    {
        Buffer paletteBuffer; // 2 - bone matrices (host-visible, mapped); concatenated per skinned instance
        std::span<glm::mat4> mappedPalettes;
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};
