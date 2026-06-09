export module RendererVK:StaticMeshGraphicsPipeline;

import Core;

import :VK;
import :Buffer;
import :Layout;
import :GraphicsPipeline;
import :IndirectExecutionSet;
import :IndirectCommandsLayout;
import :DescriptorSet;
import :Texture;
import :Sampler;

export class ObjectContainer;
export class StagingManager;
export class CommandBuffer;
export class RenderPass;

export class StaticMeshGraphicsPipeline final
{
public:
    StaticMeshGraphicsPipeline();
    ~StaticMeshGraphicsPipeline();
    StaticMeshGraphicsPipeline(const StaticMeshGraphicsPipeline&) = delete;

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        Buffer& ubo;
        Buffer& vertexBuffer;
        Buffer& indexBuffer;
        Buffer& materialInfoBuffer;
        Buffer& instanceIdxBuffer;
        Buffer& meshInstanceBuffer;
        Buffer& indirectCommandBuffer;

        Buffer& lightInfosBuffer;
		Buffer& lightGridsBuffer;
		Buffer& lightTableBuffer;

		Buffer& giGridDataBuffer;    // GI probe clipmap SH volume (read in the fragment shader)

		vk::ImageView shadowMapView;       // sun cascaded shadow map (e2DArray depth)
		vk::Sampler shadowMapSampler;      // comparison sampler for PCF
		vk::Sampler shadowMapDepthSampler; // non-comparison sampler for the PCSS blocker search
    };

    void initialize(RenderPass& renderPass);
    void reloadShaders();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);
    // Refresh the denoised-AO image binding on a descriptor set (call each frame; the draw CB is cached and
    // the AO image is recreated on resize).
    void updateAODescriptor(vk::DescriptorSet descriptorSet, vk::ImageView aoView, vk::Sampler aoSampler);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }
    const IndirectExecutionSet& getIndirectExecutionSet() const { return m_indirectExecutionSet; }
    const IndirectCommandsLayout& getIndirectCommandsLayout() const { return m_indirectCommandsLayout; }

private:

    void buildPipelineLayout(GraphicsPipelineLayout& layout);

    GraphicsPipeline m_graphicsPipeline;
    IndirectExecutionSet m_indirectExecutionSet;
    IndirectCommandsLayout m_indirectCommandsLayout;
    Sampler m_sampler;
    RenderPass* m_pRenderPass = nullptr;

    vk::DeviceSize m_preprocessSize = 0;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_preprocessBuffers;            // opaque pass
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_transparentPreprocessBuffers; // transparent pass

    // Issues one vkCmdExecuteGeneratedCommandsEXT over the given indirect/preprocess buffers.
    void recordExecuteGeneratedCommands(vk::CommandBuffer vkCommandBuffer, Buffer& indirectCommandBuffer, Buffer& preprocessBuffer, uint32 numMeshes, vk::DeviceSize indirectOffset = 0);
};