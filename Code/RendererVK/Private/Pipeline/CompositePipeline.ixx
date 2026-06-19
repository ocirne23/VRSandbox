export module RendererVK.CompositePipeline;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.GraphicsPipeline;
import RendererVK.DescriptorSet;
import RendererVK.RenderPass;

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
        vk::ImageView resolvedView; // scene colour to tonemap (TAA-resolved, or a SceneColor eye layer in VR)
        vk::ImageLayout resolvedLayout = vk::ImageLayout::eGeneral; // GENERAL for TAA, SHADER_READ_ONLY for SceneColor
        vk::Sampler   sampler;
        vk::Buffer    exposureBuffer; // eye-adaptation { avgLum, exposure } (storage)
        float exposureEV = 0.0f;    // exposure in stops; the shader gets exp2(exposureEV)
        int32 tonemapper = 0;       // 0 = off (raw clip), 1 = Reinhard, 2 = ACES, 3 = AgX
        int32 autoExposure = 0;     // 1 = multiply by the eye-adaptation exposure
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
        int32 autoExposure;
    };

    GraphicsPipeline m_graphicsPipeline;
};
