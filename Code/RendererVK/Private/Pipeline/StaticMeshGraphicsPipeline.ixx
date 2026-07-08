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

class CommandBuffer;
class ObjectContainer;

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
        Buffer& meshCountBuffer;                  // uint32 live mesh count (DGC sequenceCountAddress), CPU-written per frame

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

		vk::ImageView oceanMapsView;       // FFT ocean displacement/gradient array (VS displacement + FS shading)
		vk::Sampler oceanMapsSampler;

		uint32 viewIndex = 0; // selects u_views[viewIndex] via push constant (0 = centre/desktop, 1/2 = eyes)
    };

    void initialize(vk::RenderPass renderPass, uint32 maxUniqueMeshes, uint32 maxTextures, bool stereo = false);
    void reloadShaders(vk::RenderPass renderPass, uint32 maxTextures);
    void resizeMeshCapacity(uint32 maxUniqueMeshes);
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, RecordParams& params, bool updateDescriptors = true);
    void updateAODescriptor(vk::DescriptorSet descriptorSet, vk::ImageView aoView, vk::Sampler aoSampler);
    void updateTlasDescriptor(vk::DescriptorSet descriptorSet, vk::AccelerationStructureKHR tlas);
    // Rewrites one slot of the texture array (binding 18) with a streamed texture's current view.
    void updateTextureDescriptor(vk::DescriptorSet descriptorSet, uint32 slotIdx, vk::ImageView view);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);
    // Ocean variant: light refraction/reflection ray hits with the grid lights (OCEAN_HIT_LIGHTS define).
    // Takes effect on the next reloadShaders (the Renderer reloads when the tweak flips).
    void setOceanHitLights(bool enabled) { m_oceanHitLights = enabled; }
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
    bool m_oceanHitLights = false; // OCEAN_HIT_LIGHTS define on the ocean fragment variant

    vk::DeviceSize m_preprocessSize = 0;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_preprocessBuffers;            // opaque pass
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_transparentPreprocessBuffers; // transparent pass

    void createPreprocessBuffers(uint32 maxUniqueMeshes);
    void recordExecuteGeneratedCommands(vk::CommandBuffer vkCommandBuffer, Buffer& indirectCommandBuffer, Buffer& preprocessBuffer, Buffer& meshCountBuffer);
};