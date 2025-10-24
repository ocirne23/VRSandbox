export module RendererVK.IndirectCullComputePipeline;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.ComputePipeline;
import RendererVK.Layout;
import RendererVK.Transform;
import RendererVK.DescriptorSet;

export class ObjectContainer;

export class IndirectCullComputePipeline final
{
public:
    IndirectCullComputePipeline();
    ~IndirectCullComputePipeline();
    IndirectCullComputePipeline(const IndirectCullComputePipeline&) = delete;

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        Buffer& ubo;                          // 0
        Buffer& inRenderNodeTransformsBuffer; // 1
        Buffer& inMeshInstancesBuffer;        // 2
        Buffer& inMeshInstanceOffsetsBuffer;  // 3
        Buffer& inMeshInfoBuffer;             // 4
        Buffer& inFirstInstancesBuffer;       // 5
    };

    bool initialize();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& recordParams);
    void update(uint32 frameIdx, uint32 numMeshInstances);

    Buffer& getIndirectCommandBuffer(uint32 idx)  { return m_perFrameData[idx].outIndirectCommandBuffer; }
    Buffer& getInstanceIdxBuffer(uint32 idx)      { return m_perFrameData[idx].outMeshInstanceIndexesBuffer; }
    Buffer& getOutMeshInstancesBuffer(uint32 idx) { return m_perFrameData[idx].outMeshInstancesBuffer; }
    
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_computePipeline.getDescriptorSetLayout(); }

private:

    ComputePipeline m_computePipeline;

    struct PerFrameData
    {
        Buffer inIndirectCommandBuffer;
        Buffer outMeshInstancesBuffer;
        Buffer outMeshInstanceIndexesBuffer;
        Buffer outIndirectCommandBuffer;

        std::span<vk::DispatchIndirectCommand> mappedIndirectCommands;
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};