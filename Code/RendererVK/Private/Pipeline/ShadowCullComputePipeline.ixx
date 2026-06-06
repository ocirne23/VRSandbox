export module RendererVK:ShadowCullComputePipeline;

import Core;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :Layout;
import :DescriptorSet;

export class ShadowCullComputePipeline final
{
public:
    ShadowCullComputePipeline();
    ~ShadowCullComputePipeline();
    ShadowCullComputePipeline(const ShadowCullComputePipeline&) = delete;

    struct RecordParams
    {
        DescriptorSet& descriptorSet;
        Buffer& ubo;                          // 0 - bound for layout compatibility (frustum unused)
        Buffer& dispatchIndirectBuffer;       // dispatch size { numInstances, 1, 1 }
        Buffer& inRenderNodeTransformsBuffer; // 1
        Buffer& inMeshInstancesBuffer;        // 2
        Buffer& inMeshInstanceOffsetsBuffer;  // 3
        Buffer& inMeshInfoBuffer;             // 4
        Buffer& inFirstInstancesBuffer;       // 5
        Buffer& inMaterialInfoBuffer;         // 9 - resolves the alpha-mask texture per caster
    };

    void initialize();
    void reloadShaders();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);

    Buffer& getIndirectCommandBuffer(uint32 frameIdx) { return m_perFrameData[frameIdx].outIndirectCommandBuffer; }
    Buffer& getInstanceIdxBuffer(uint32 frameIdx)     { return m_perFrameData[frameIdx].outMeshInstanceIndexesBuffer; }
    Buffer& getOutMeshInstancesBuffer(uint32 frameIdx){ return m_perFrameData[frameIdx].outMeshInstancesBuffer; }

    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_computePipeline.getDescriptorSetLayout(); }

private:

    void buildComputeLayout(ComputePipelineLayout& layout);

    ComputePipeline m_computePipeline;

    struct PerFrameData
    {
        Buffer outMeshInstancesBuffer;       // 6
        Buffer outMeshInstanceIndexesBuffer; // 7
        Buffer outIndirectCommandBuffer;     // 8 - [0..MAX) opaque, [MAX..2*MAX) transparent
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};
