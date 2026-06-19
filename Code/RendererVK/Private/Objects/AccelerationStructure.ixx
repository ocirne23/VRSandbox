export module RendererVK.AccelerationStructure;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.Layout;

// Hardware ray-tracing acceleration structures for GI. One bottom-level AS (BLAS) per unique mesh, built
// straight from the shared vertex/index mega-buffers; a single top-level AS (TLAS) rebuilt every frame
// from a GPU-written instance buffer. The TLAS is ray-queried by the GI probe-trace compute shader.
export class AccelerationStructure final
{
public:
    AccelerationStructure();
    ~AccelerationStructure();
    AccelerationStructure(const AccelerationStructure&) = delete;

    void initialize(uint32 maxUniqueMeshes);
    // Grows the BLAS-address buffer for a larger unique-mesh capacity, preserving its contents
    // (GPU must be idle).
    void resizeBlasAddressBuffer(uint32 maxUniqueMeshes);

    // Records BLAS builds for meshes [firstMesh, firstMesh+count) into cmd, reading geometry directly
    // from the shared vertex/index buffers. totalVertices is the current vertex-buffer fill (in vertices)
    // and is only used to derive a safe in-bounds maxVertex per mesh. BLASes are built once per mesh.
    void recordBuildBlas(vk::CommandBuffer cmd, Buffer& vertexBuffer, Buffer& indexBuffer,
        const RendererVKLayout::MeshInfo* meshInfos, uint32 firstMesh, uint32 count, uint32 totalVertices);

    // Records a per-frame TLAS (re)build from numInstances pre-written VkAccelerationStructureInstanceKHR
    // records in instanceBuffer. The TLAS is double-buffered (one per frame-in-flight): it is rebuilt
    // every frame and ray-queried in the same frame, so a single shared TLAS would be write-after-read
    // raced by the next frame's rebuild (pipelined across submits on one queue) -> device lost.
    // Returns true when the TLAS handle changed (first build / capacity growth).
    bool recordBuildTlas(vk::CommandBuffer cmd, uint32 frameIdx, Buffer& instanceBuffer, uint32 numInstances);

    // mesh idx -> BLAS device address (uint64), consumed by the TLAS-instance compute shader.
    Buffer& getBlasAddressBuffer() { return m_blasAddressBuffer; }
    uint32 getNumBlas() const { return m_numBlas; }

    vk::AccelerationStructureKHR getTlas(uint32 frameIdx) const { return m_tlas[frameIdx]; }

private:
    void ensureScratch(Buffer& scratch, vk::DeviceAddress& outAlignedAddr, vk::DeviceSize needed);

    struct Blas
    {
        vk::AccelerationStructureKHR handle = nullptr;
        Buffer buffer;
    };
    std::vector<Blas> m_blasList;
    Buffer m_blasScratch;
    vk::DeviceAddress m_blasScratchAlignedAddr = 0;
    Buffer m_blasAddressBuffer;
    std::span<uint64> m_mappedBlasAddresses;
    uint32 m_numBlas = 0;

    // Double-buffered TLAS (one per frame-in-flight) to avoid a cross-frame WAR hazard on the structure.
    std::array<vk::AccelerationStructureKHR, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlas{};
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasBuffer;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasScratch;
    std::array<vk::DeviceAddress, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasScratchAlignedAddr{};
    std::array<uint32, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasCapacity{};

    uint32 m_scratchAlignment = 256;
};
