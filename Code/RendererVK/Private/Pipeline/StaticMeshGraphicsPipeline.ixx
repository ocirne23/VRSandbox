export module RendererVK.StaticMeshGraphicsPipeline;

import Core;

import RendererVK.VK;
import RendererVK.GraphicsPipeline;
import RendererVK.DescriptorSet;
import RendererVK.Texture;
import RendererVK.Sampler;

export class ObjectContainer;
export class StagingManager;
export class Buffer;
export class CommandBuffer;

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
    };

    bool initialize(RenderPass& renderPass);
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }

private:

    GraphicsPipeline m_graphicsPipeline;

    Texture m_colorTex;
    //Texture m_normalTex;
    //Texture m_rmhTex;
    Sampler m_sampler;
};