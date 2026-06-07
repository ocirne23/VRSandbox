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

    // Writes the current volume min corner (probe-coordinate units) into the host-visible volume buffer.
    void setVolumeMin(glm::ivec3 volumeMin);

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

    Buffer& getTlasInstanceBuffer() { return m_tlasInstanceBuffer; }
    Buffer& getProbeShBuffer()      { return m_probeSh; }
    Buffer& getGiVolumeBuffer()     { return m_giVolumeBuffer; }

private:
    void buildTlasInstanceLayout(ComputePipelineLayout& layout);
    void buildTraceLayout(ComputePipelineLayout& layout);

    ComputePipeline m_tlasInstancePipeline;
    ComputePipeline m_tracePipeline;

    Buffer m_probeSh;            // persistent SH-L1 volume (GI_PROBE_COUNT * GI_SH_STRIDE floats)
    Buffer m_giVolumeBuffer;     // host-visible ivec4 volume min corner
    std::span<int32> m_mappedVolume;
    Buffer m_tlasInstanceBuffer;

    Sampler m_textureSampler;

    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasInstanceSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_traceSets;

    bool m_cleared = false;
};
