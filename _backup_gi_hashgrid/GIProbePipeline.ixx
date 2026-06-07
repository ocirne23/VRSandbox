export module RendererVK:GIProbePipeline;

import Core;
import Core.glm;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :DescriptorSet;
import :Sampler;
import :Layout;

// Diffuse GI probe system. Owns the three compute passes and the persistent probe storage:
//   1. TLAS-instance pass : writes the per-instance VkAccelerationStructureInstanceKHR array on the GPU.
//   2. Alloc pass         : inserts the camera-region grids into the persistent probe hash table.
//   3. Trace pass         : ray-queries the TLAS per probe, shades hits (reusing the light grid + sun),
//                           and temporally blends the result into each probe's SH-L1.
// The TLAS itself is built by the AccelerationStructure object between passes 1 and 3 (orchestrated by
// the Renderer), reading the instance buffer this pipeline produces.
export class GIProbePipeline final
{
public:
    void initialize();
    void reloadShaders();

    // One-time clear of the persistent probe table (-> EMPTY), meta (-> 0) and SH (-> 0). Recorded the
    // first frame; afterwards the probe state persists and accumulates across frames.
    void recordClearPersistent(CommandBuffer& commandBuffer);
    bool needsClear() const { return !m_cleared; }
    void markCleared() { m_cleared = true; }

    struct TlasInstanceParams
    {
        Buffer& renderNodeTransforms;
        Buffer& meshInstances;   // InMeshInstance (cull input) buffer
        Buffer& instanceOffsets;
        Buffer& blasAddresses;   // mesh idx -> uint64 BLAS device address
        uint32 numInstances;
    };
    void recordTlasInstances(CommandBuffer& commandBuffer, uint32 frameIdx, TlasInstanceParams& params);

    void recordAlloc(CommandBuffer& commandBuffer, uint32 frameIdx, glm::ivec3 regionMin, uint32 regionDim);

    struct TraceParams
    {
        Buffer& ubo;
        Buffer& lightInfos;
        Buffer& lightGrid;
        Buffer& lightTable;
        Buffer& vertexBuffer;
        Buffer& indexBuffer;
        Buffer& meshInfos;
        Buffer& meshInstances;   // InMeshInstance buffer (instanceCustomIndex indexes into this)
        Buffer& materialInfos;
        vk::AccelerationStructureKHR tlas;
        vk::ImageView shadowMapView;
        vk::Sampler shadowMapSampler;
        glm::ivec3 regionMin;
        uint32 regionDim;
        uint32 frameIndex;
    };
    void recordTrace(CommandBuffer& commandBuffer, uint32 frameIdx, TraceParams& params);

    Buffer& getTlasInstanceBuffer() { return m_tlasInstanceBuffer; }
    Buffer& getProbeTableBuffer()   { return m_probeTable; }
    Buffer& getProbeGridDataBuffer(){ return m_probeGridData; }
    Buffer& getProbeShBuffer()      { return m_probeSh; }
    Buffer& getProbeMetaBuffer()    { return m_probeMeta; }

private:
    void buildTlasInstanceLayout(ComputePipelineLayout& layout);
    void buildAllocLayout(ComputePipelineLayout& layout);
    void buildTraceLayout(ComputePipelineLayout& layout);

    ComputePipeline m_tlasInstancePipeline;
    ComputePipeline m_allocPipeline;
    ComputePipeline m_tracePipeline;

    // Persistent probe storage (not per-frame; accumulates across frames).
    Buffer m_probeTable;
    Buffer m_probeGridData;
    Buffer m_probeSh;
    Buffer m_probeMeta;
    Buffer m_tlasInstanceBuffer;

    Sampler m_textureSampler;

    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasInstanceSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_allocSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_traceSets;

    bool m_cleared = false;
};
