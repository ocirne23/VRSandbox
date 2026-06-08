export module RendererVK:GIProbePipeline;

import Core;
import Core.glm;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :GraphicsPipeline;
import :RenderPass;
import :DescriptorSet;
import :Sampler;
import :Layout;

// Diffuse GI probe system over a persistent, world-space probe hash grid (sibling of the light grid). The
// grid buffers (hash table, grid data / SH, work list) are ping-ponged prev/cur across frames so probe
// irradiance is carried forward temporally. Three compute passes per frame:
//   1. TLAS-instance pass : writes the per-instance VkAccelerationStructureInstanceKHR array on the GPU.
//   2. Alloc+carry pass   : inserts camera-region cubes into the cur grid and carries SH from the prev grid.
//   3. Trace pass         : ray-queries the TLAS per probe cell, shades hits (reusing the light grid + sun),
//                           and temporally blends the result into each cell's SH-L1.
// The TLAS is built by the AccelerationStructure object between passes 1 and 2 (orchestrated by the Renderer).
export class GIProbePipeline final
{
public:
    void initialize();
    void reloadShaders();

    // One-time clear of BOTH ping-pong grid tables (so the first frames' prev lookups are empty).
    void recordClearPersistent(CommandBuffer& commandBuffer);
    bool needsClear() const { return !m_cleared; }
    void markCleared() { m_cleared = true; }
    void doClear() { m_cleared = false; }

    struct TlasInstanceParams
    {
        Buffer& renderNodeTransforms;
        Buffer& meshInstances;   // InMeshInstance (cull input) buffer
        Buffer& instanceOffsets;
        Buffer& blasAddresses;   // mesh idx -> uint64 BLAS device address
        uint32 numInstances;
    };
    void recordTlasInstances(CommandBuffer& commandBuffer, uint32 frameIdx, TlasInstanceParams& params);

    struct AllocParams
    {
        glm::ivec3 regionMin; // grid-cube coord of the region's min corner
        uint32     regionDim; // cubes per axis (2*radius + 1)
        glm::vec3  viewPos;   // camera position (drives per-cube cellSize)
    };
    // Clears the cur grid table/work-list and inserts+carries the camera-region cubes.
    void recordAlloc(CommandBuffer& commandBuffer, uint32 frameIdx, const AllocParams& params);

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
        uint32 frameIndex;       // free-running frame counter (RNG seed)
    };
    void recordTrace(CommandBuffer& commandBuffer, uint32 frameIdx, TraceParams& params);

    // Procedural sky used for probe rays that miss all geometry. The sun disc/glow is driven by the
    // dynamic UBO sun direction/color (setSunLight); these control the gradient and intensity.
    struct SkyParams
    {
        glm::vec3 zenith   = glm::vec3(0.20f, 0.40f, 0.85f);
        glm::vec3 horizon  = glm::vec3(0.55f, 0.65f, 0.85f);
        glm::vec3 ground   = glm::vec3(0.20f, 0.20f, 0.22f);
        glm::vec3 up       = glm::vec3(0.0f, 1.0f, 0.0f); // sky-up axis; set to the local up on a planet
        float intensity     = 1.0f;
        float sunAngularCos = 0.9999f; // ~0.8 deg disc; 1.0 disables the disc
        float sunGlow       = 0.0f;    // 0 = none; e.g. 256 for a tight bloom around the sun
    };
    void setSkyParams(const SkyParams& sky) { m_skyParams = sky; }

    // Debug visualization: instanced cubes at every live probe cell, drawn into the main color pass.
    // initializeDebug must be called after the main render pass exists.
    void initializeDebug(const RenderPass& renderPass);
    void reloadDebugShaders();
    void recordDebugDraw(CommandBuffer& commandBuffer, uint32 frameIdx, Buffer& ubo, float radius, uint32 mode);

    Buffer& getTlasInstanceBuffer(uint32 frameIdx) { return m_tlasInstanceBuffer[frameIdx]; }
    // cur grid buffers for the given frame (consumed by the main pass's fragment shader).
    Buffer& getGiGridDataBuffer(uint32 frameIdx) { return m_giGridData[frameIdx]; }
    Buffer& getGiTableBuffer(uint32 frameIdx)    { return m_giTable[frameIdx]; }

private:
    void buildTlasInstanceLayout(ComputePipelineLayout& layout);
    void buildAllocLayout(ComputePipelineLayout& layout);
    void buildTraceLayout(ComputePipelineLayout& layout);
    void buildDebugLayout(GraphicsPipelineLayout& layout);
    void recordClearCur(CommandBuffer& commandBuffer, uint32 frameIdx);

    ComputePipeline m_tlasInstancePipeline;
    ComputePipeline m_allocPipeline;
    ComputePipeline m_tracePipeline;
    GraphicsPipeline m_debugPipeline;
    const RenderPass* m_debugRenderPass = nullptr;

    SkyParams m_skyParams;

    // Persistent probe grid, ping-ponged by frame-in-flight index (prev = the other index).
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_giTable;    // { numGrids, gridCounter, tableSize, table[] }
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_giGridData; // headers + SH cells
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_giGridList; // { count, gridIdx[] }

    // Per-frame instance buffer: written each frame by the TLAS-instance compute and consumed by that
    // frame's TLAS build; double-buffered for the same cross-frame-hazard reason as the TLAS itself.
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasInstanceBuffer;

    Sampler m_textureSampler;

    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasInstanceSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_allocSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_traceSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_debugSets;

    bool m_cleared = false;
};
