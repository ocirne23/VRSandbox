export module RendererVK:CompositePipeline;

import Core;

import :VK;
import :CommandBuffer;
import :GraphicsPipeline;
import :DescriptorSet;
import :RenderPass;

// Fullscreen composite: samples the TAA-resolved scene colour and writes it into the swapchain (drawn
// before ImGui in the swapchain render pass). This is the HDR -> display mapping: exposure and the
// selected tonemap operator run here (composite.fs.glsl), everything upstream is linear radiance.
// No vertex buffers; a single combined-image-sampler binding plus a small push-constant block.
export class CompositePipeline final
{
public:
    void initialize(const RenderPass& renderPass);
    void reloadShaders(const RenderPass& renderPass);

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        vk::ImageView resolvedView; // TAA-resolved scene colour for this frame (GENERAL layout)
        vk::Sampler   sampler;
        float exposureEV = 0.0f;    // exposure in stops; the shader gets exp2(exposureEV)
        int32 tonemapper = 0;       // 0 = off (raw clip), 1 = Reinhard, 2 = ACES, 3 = AgX
    };
    void record(CommandBuffer& commandBuffer, const RecordParams& params);

    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }

private:
    void buildPipelineLayout(GraphicsPipelineLayout& layout);

    // Push constants; must match the PostPC block in composite.fs.glsl.
    struct CompositePC
    {
        float exposure;   // linear scale, exp2 of the EV tweak
        int32 tonemapper;
    };

    GraphicsPipeline m_graphicsPipeline;
};
