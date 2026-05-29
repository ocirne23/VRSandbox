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
        Buffer& transparentIndirectCommandBuffer;

        Buffer& lightInfosBuffer;
		Buffer& lightGridsBuffer;
		Buffer& lightTableBuffer;
    };

    void initialize(RenderPass& renderPass);
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_graphicsPipeline.getDescriptorSetLayout(); }
    const IndirectExecutionSet& getIndirectExecutionSet() const { return m_indirectExecutionSet; }
    const IndirectCommandsLayout& getIndirectCommandsLayout() const { return m_indirectCommandsLayout; }

private:

    GraphicsPipeline m_graphicsPipeline;
    IndirectExecutionSet m_indirectExecutionSet;
    IndirectCommandsLayout m_indirectCommandsLayout;
    Sampler m_sampler;

    // Device-generated-commands execute path (built only when the extension is supported).
    bool m_useDeviceGeneratedCommands = false;
    vk::DeviceSize m_preprocessSize = 0;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_preprocessBuffers;            // opaque pass
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_transparentPreprocessBuffers; // transparent pass

    // Issues one vkCmdExecuteGeneratedCommandsEXT over the given indirect/preprocess buffers.
    void recordExecuteGeneratedCommands(vk::CommandBuffer vkCommandBuffer, Buffer& indirectCommandBuffer, Buffer& preprocessBuffer, uint32 numMeshes);
};