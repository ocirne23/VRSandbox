export module RendererVK:ShadowMapGraphicsPipeline;

import Core;

import :VK;
import :Buffer;
import :Layout;
import :GraphicsPipeline;
import :IndirectCommandsLayout;
import :DescriptorSet;
import :Sampler;
import :ShadowMap;

export class CommandBuffer;

// Depth-only pipeline that renders the shadow caster list into one cascade of the sun ShadowMap.
// Mirrors StaticMeshGraphicsPipeline: it consumes the cull's IndirectDrawSequence buffer through
// vkCmdExecuteGeneratedCommandsEXT so the draw path matches the main pass. All cascades render in one
// multiview render pass (gl_ViewIndex picks the cascade matrix); because multiview disallows an Indirect
// Execution Set, DGC here uses no execution set and binds the single depth-only pipeline directly.
export class ShadowMapGraphicsPipeline final
{
public:
    ShadowMapGraphicsPipeline();
    ~ShadowMapGraphicsPipeline();
    ShadowMapGraphicsPipeline(const ShadowMapGraphicsPipeline&) = delete;

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        Buffer& ubo;                   // 0 - main UBO (holds cascadeViewProj[]; cascade chosen by gl_ViewIndex)
        Buffer& meshInstanceBuffer;    // 1 - shadow cull's transformed instances (carry the alpha-mask tex idx)
        Buffer& vertexBuffer;
        Buffer& indexBuffer;
        Buffer& instanceIdxBuffer;     // vertex binding 2 - shadow cull's compacted instance indices
        Buffer& indirectCommandBuffer; // shadow cull's IndirectDrawSequence buffer (opaque region)
    };

    void initialize(ShadowMap& shadowMap);
    void reloadShaders();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);

    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }

private:

    void buildPipelineLayout(GraphicsPipelineLayout& layout);
    void buildIndirectState();

    GraphicsPipeline m_graphicsPipeline;
    IndirectCommandsLayout m_indirectCommandsLayout;
    Sampler m_sampler; // for the diffuse texture array used by the alpha-mask discard
    vk::RenderPass m_renderPass;

    vk::DeviceSize m_preprocessSize = 0;
    // One preprocess scratch per frame in flight (a single multiview execute renders all cascades).
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_preprocessBuffers;
};
