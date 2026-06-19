export module RendererVK.StaticMeshGraphicsPipeline;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.Layout;
import RendererVK.GraphicsPipeline;
import RendererVK.IndirectExecutionSet;
import RendererVK.IndirectCommandsLayout;
import RendererVK.DescriptorSet;
import RendererVK.Texture;
import RendererVK.Sampler;
import RendererVK.fwd;

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
        Buffer& indirectCommandBuffer;            // opaque draw sequences
        Buffer& transparentIndirectCommandBuffer; // transparent draw sequences

        Buffer& lightInfosBuffer;
		Buffer& lightGridsBuffer;
		Buffer& lightTableBuffer;

		Buffer& giGridDataBuffer;    // GI probe clipmap SH volume (read in the fragment shader)

		Buffer& meshInfoBuffer;        // shadow-ray alpha test (firstIndex/vertexOffset per mesh)
		Buffer& rtMeshInstancesBuffer; // the unculled instance list the TLAS records index into

		vk::ImageView shadowMapView;       // sun cascaded shadow map (e2DArray depth)
		vk::Sampler shadowMapSampler;      // comparison sampler for PCF
		vk::Sampler shadowMapDepthSampler; // non-comparison sampler for the PCSS blocker search

		vk::ImageView gbufferDepthView;    // full-res depth (AO bilateral upsample edge weights)
		vk::Sampler gbufferSampler;

		uint32 viewIndex = 0; // selects u_views[viewIndex] via push constant (0 = centre/desktop, 1/2 = eyes)
    };

    void initialize(vk::RenderPass renderPass, uint32 maxUniqueMeshes, uint32 maxTextures, bool stereo = false);
    void reloadShaders(vk::RenderPass renderPass, uint32 maxTextures);
    void resizeMeshCapacity(uint32 maxUniqueMeshes);
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params, bool updateDescriptors = true);
    void updateAODescriptor(vk::DescriptorSet descriptorSet, vk::ImageView aoView, vk::Sampler aoSampler);
    void updateTlasDescriptor(vk::DescriptorSet descriptorSet, vk::AccelerationStructureKHR tlas);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }
    const IndirectExecutionSet& getIndirectExecutionSet() const { return m_indirectExecutionSet; }
    const IndirectCommandsLayout& getIndirectCommandsLayout() const { return m_indirectCommandsLayout; }

private:

    void buildPipelineLayout(GraphicsPipelineLayout& layout, uint32 maxTextures);

    GraphicsPipeline m_graphicsPipeline;
    IndirectExecutionSet m_indirectExecutionSet;
    IndirectCommandsLayout m_indirectCommandsLayout;
    Sampler m_sampler;
    vk::RenderPass m_renderPass;
    bool m_stereo = false;

    vk::DeviceSize m_preprocessSize = 0;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_preprocessBuffers;            // opaque pass
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_transparentPreprocessBuffers; // transparent pass

    void createPreprocessBuffers(uint32 maxUniqueMeshes);
    void recordExecuteGeneratedCommands(vk::CommandBuffer vkCommandBuffer, Buffer& indirectCommandBuffer, Buffer& preprocessBuffer, uint32 numMeshes);
};