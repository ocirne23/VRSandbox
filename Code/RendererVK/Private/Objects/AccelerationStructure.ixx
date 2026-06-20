export module RendererVK:AccelerationStructure;

import Core;
import :VK;
import :Buffer;
import :Layout;

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

    // A skinned mesh's deformed output region. vertexCount/firstIndex/indexCount are known exactly, so the
    // BLAS is built without relying on neighbouring mesh offsets.
    struct SkinnedBlasBuild
    {
        uint32 meshIdx;       // index into the per-mesh BLAS-address buffer
        uint32 vertexOffset;  // MeshVertex units, into the shared vertex buffer
        uint32 vertexCount;
        uint32 firstIndex;
        uint32 indexCount;
    };

    // Rebuilds the skinned meshes' BLASes from their (this-frame) deformed vertices. Double-buffered per
    // frame-in-flight (the previous frame's TLAS may still reference the other slot), and their addresses
    // are written into that frame's BLAS-address buffer. BLAS buffers are allocated once (geometry size is
    // constant) and rebuilt in place each frame. Call after the skinning compute, before the TLAS build.
    void recordBuildSkinnedBlas(vk::CommandBuffer cmd, uint32 frameIdx, Buffer& vertexBuffer, Buffer& indexBuffer,
        std::span<const SkinnedBlasBuild> builds);

    // Records a per-frame TLAS (re)build from numInstances pre-written VkAccelerationStructureInstanceKHR
    // records in instanceBuffer. The TLAS is double-buffered (one per frame-in-flight): it is rebuilt
    // every frame and ray-queried in the same frame, so a single shared TLAS would be write-after-read
    // raced by the next frame's rebuild (pipelined across submits on one queue) -> device lost.
    // Returns true when the TLAS handle changed (first build / capacity growth).
    bool recordBuildTlas(vk::CommandBuffer cmd, uint32 frameIdx, Buffer& instanceBuffer, uint32 numInstances);

    // mesh idx -> BLAS device address (uint64), consumed by the TLAS-instance compute shader. Per frame
    // in flight: skinned meshes have a different BLAS per slot, so the address differs between frames.
    Buffer& getBlasAddressBuffer(uint32 frameIdx) { return m_blasAddressBuffers[frameIdx]; }
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
    // Per frame in flight (skinned BLAS addresses differ between slots; static addresses are written to all).
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_blasAddressBuffers;
    std::array<std::span<uint64>, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_mappedBlasAddresses;
    uint32 m_numBlas = 0;

    // Double-buffered skinned BLASes (rebuilt every frame) + per-frame scratch. Allocated lazily/in place.
    std::array<std::vector<Blas>, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_skinnedBlas;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_skinnedBlasScratch;
    std::array<vk::DeviceAddress, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_skinnedBlasScratchAlignedAddr{};

    // Double-buffered TLAS (one per frame-in-flight) to avoid a cross-frame WAR hazard on the structure.
    std::array<vk::AccelerationStructureKHR, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlas{};
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasBuffer;
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasScratch;
    std::array<vk::DeviceAddress, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasScratchAlignedAddr{};
    std::array<uint32, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_tlasCapacity{};

    uint32 m_scratchAlignment = 256;
};
