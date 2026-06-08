export module RendererVK:GBufferPipeline;

import Core;

import :VK;
import :Buffer;
import :CommandBuffer;
import :GraphicsPipeline;
import :DescriptorSet;
import :Layout;
import :GBuffer;

// Depth + world-normal prepass. Rasterizes the camera-visible geometry (reusing the camera cull's
// indirect draw + transformed-instance + instance-index buffers) into a GBuffer (normal color target +
// depth). No DGC/execution set is needed: a single pipeline draws every mesh via vkCmdDrawIndexedIndirect
// over the existing IndirectDrawSequence buffer (offset 4 to skip the leading pipelineIndex field).
export class GBufferPipeline final
{
public:
    void initialize(const GBuffer& gbuffer);
    void reloadShaders();

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        Buffer& ubo;
        Buffer& meshInstanceBuffer;   // camera cull OutMeshInstance buffer (vertex shader reads via inst_idx)
        Buffer& vertexBuffer;
        Buffer& indexBuffer;
        Buffer& instanceIdxBuffer;    // per-instance uint -> index into meshInstanceBuffer
        Buffer& indirectCommandBuffer; // IndirectDrawSequence[]; drawn with offset 4, stride 24
    };
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);

    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }

private:
    void buildPipelineLayout(GraphicsPipelineLayout& layout);

    vk::RenderPass m_renderPass;
    GraphicsPipeline m_graphicsPipeline;
};
