export module RendererVK.ShadowCullComputePipeline;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.ComputePipeline;
import RendererVK.Layout;
import RendererVK.DescriptorSet;

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

    void initialize(uint32 maxMeshInstances, uint32 maxUniqueMeshes);
    void reloadShaders();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params);
    // Capacity growth (caller must have the GPU idle and re-record command buffers afterwards).
    void resizeInstanceBuffers(uint32 maxMeshInstances);
    void resizeCommandBuffers(uint32 maxUniqueMeshes);

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
        Buffer outIndirectCommandBuffer;     // 8 - single opaque region
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};
