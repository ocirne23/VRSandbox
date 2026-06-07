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

// Diffuse GI probe system over a fixed DIM^3 camera-anchored probe volume (no hash grid). Two compute
// passes:
//   1. TLAS-instance pass : writes the per-instance VkAccelerationStructureInstanceKHR array on the GPU.
//   2. Trace pass         : ray-queries the TLAS per probe, shades hits (reusing the light grid + sun),
//                           and temporally blends the result into each probe's SH-L1.
// The TLAS is built by the AccelerationStructure object between passes 1 and 2 (orchestrated by the
// Renderer). The volume's min corner is written to a small buffer each frame (read by the fragment pass).
export class GIProbePipeline final
{
public:
    void initialize();
    void reloadShaders();

    // One-time clear of the persistent probe SH (it accumulates across frames thereafter).
    void recordClearPersistent(CommandBuffer& commandBuffer);
    bool needsClear() const { return !m_cleared; }
    void markCleared() { m_cleared = true; }

    // Writes the current volume min corner (probe-coordinate units) into this frame's volume buffer.
    void setVolumeMin(uint32 frameIdx, glm::ivec3 volumeMin);

    struct TlasInstanceParams
    {
        Buffer& renderNodeTransforms;
        Buffer& meshInstances;   // InMeshInstance (cull input) buffer
        Buffer& instanceOffsets;
        Buffer& blasAddresses;   // mesh idx -> uint64 BLAS device address
        uint32 numInstances;
    };
    void recordTlasInstances(CommandBuffer& commandBuffer, uint32 frameIdx, TlasInstanceParams& params);

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
        glm::ivec3 volumeMin;
        uint32 frameIndex;
    };
    void recordTrace(CommandBuffer& commandBuffer, uint32 frameIdx, TraceParams& params);

    Buffer& getTlasInstanceBuffer(uint32 frameIdx) { return m_tlasInstanceBuffer[frameIdx]; }
    Buffer& getProbeShBuffer()      { return m_probeSh; }
    Buffer& getGiVolumeBuffer(uint32 frameIdx) { return m_giVolumeBuffer[frameIdx]; }

private:
    void buildTlasInstanceLayout(ComputePipelineLayout& layout);
    void buildTraceLayout(ComputePipelineLayout& layout);

    ComputePipeline m_tlasInstancePipeline;
    ComputePipeline m_tracePipeline;

    Buffer m_probeSh;            // persistent SH-L1 volume (GI_PROBE_COUNT * GI_SH_STRIDE floats)
    // Per-frame volume min corner (host-visible ivec4): written by the CPU each frame and read by that
    // frame's fragment pass; double-buffered so the CPU write can't race the previous frame's GPU read.
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_giVolumeBuffer;
    std::array<std::span<int32>, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_mappedVolume;
    // Per-frame instance buffer: written each frame by the TLAS-instance compute and consumed by that
    // frame's TLAS build; double-buffered for the same cross-frame-hazard reason as the TLAS itself.
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasInstanceBuffer;

    Sampler m_textureSampler;

    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasInstanceSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_traceSets;

    bool m_cleared = false;
};
