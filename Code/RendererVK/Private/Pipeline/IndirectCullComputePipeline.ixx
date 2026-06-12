export module RendererVK:IndirectCullComputePipeline;

import Core;
import Core.Transform;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :Layout;
import :DescriptorSet;

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

    void initialize(uint32 maxMeshInstances, uint32 maxUniqueMeshes);
    void reloadShaders();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& recordParams);
    void update(uint32 frameIdx, uint32 numMeshInstances);
    // Capacity growth (caller must have the GPU idle and re-record command buffers afterwards).
    void resizeInstanceBuffers(uint32 maxMeshInstances);
    void resizeCommandBuffers(uint32 maxUniqueMeshes);

    Buffer& getIndirectCommandBuffer(uint32 idx)   { return m_perFrameData[idx].outIndirectCommandBuffer; }
    Buffer& getTransparentIndirectCommandBuffer(uint32 idx) { return m_perFrameData[idx].outTransparentIndirectCommandBuffer; }
    Buffer& getInstanceIdxBuffer(uint32 idx)      { return m_perFrameData[idx].outMeshInstanceIndexesBuffer; }
    Buffer& getOutMeshInstancesBuffer(uint32 idx) { return m_perFrameData[idx].outMeshInstancesBuffer; }
    // Dispatch-size buffer { numInstances, 1, 1 }; reused by the shadow cull which dispatches the
    // same instance set against each cascade's frustum.
    Buffer& getDispatchIndirectBuffer(uint32 idx) { return m_perFrameData[idx].inIndirectCommandBuffer; }
    
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_computePipeline.getDescriptorSetLayout(); }

private:

    void buildComputeLayout(ComputePipelineLayout& layout);

    ComputePipeline m_computePipeline;

    struct PerFrameData
    {
        Buffer inIndirectCommandBuffer;
        Buffer outMeshInstancesBuffer;            // 6
        Buffer outMeshInstanceIndexesBuffer;      // 7
        Buffer outIndirectCommandBuffer;          // 8 - opaque
        Buffer outTransparentIndirectCommandBuffer; // 9 - transparent

        std::span<vk::DispatchIndirectCommand> mappedIndirectCommands;
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};