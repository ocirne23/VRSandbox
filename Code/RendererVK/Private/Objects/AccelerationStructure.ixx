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

    // RT mesh aliasing: a LOD chain shares ONE BLAS (rays don't need per-level fidelity), so every level
    // aliases the chain's RT level. Aliased meshes never build their own BLAS; their address entries are
    // filled from the alias target's, and the TLAS-instance writer packs the aliased meshIdx into each
    // instance's sbtOffset so hit shaders fetch the matching geometry. Grown identity-filled by
    // setNumMeshes (Renderer::addMeshInfos); addMeshLodGroup points chain levels at the RT level.
    void setNumMeshes(uint32 numMeshes);
    void setMeshAlias(uint32 meshIdx, uint32 aliasIdx);
    uint32 getMeshAlias(uint32 meshIdx) const { return meshIdx < m_numAliasMeshes ? m_mappedMeshAlias[meshIdx] : meshIdx; }
    Buffer& getMeshAliasBuffer() { return m_meshAliasBuffer; }

    // Mesh streaming eviction: zeroes the mesh's BLAS-address entries (the TLAS writer masks null-BLAS
    // instances) and, when the mesh owns the BLAS (self-aliased), destroys it and zeroes every entry
    // aliased to it. Only called for sets cold long enough that no in-flight TLAS references the BLAS.
    void onMeshEvicted(uint32 meshIdx);

    // Records BLAS builds for the given meshes into cmd, reading geometry directly from the shared
    // vertex/index buffers with each mesh's exact vertex count (maxVertex must be tight — see the
    // comment in the implementation). Aliased and streamed-out (indexCount 0) meshes are skipped; the
    // address-fill pass afterwards refreshes every aliased mesh's entry from its target.
    // allowCompaction: builds carry eAllowCompaction and record a compacted-size query for
    // recordCompaction to consume.
    void recordBuildBlas(vk::CommandBuffer cmd, Buffer& vertexBuffer, Buffer& indexBuffer,
        const RendererVKLayout::MeshInfo* meshInfos, const uint32* vertexCounts, std::span<const uint32> meshIndices,
        bool allowCompaction);

    // BLAS compaction: build-size quotes are conservative worst cases, so freshly built BLASes waste
    // 30-50% of their memory. Once a build batch's compacted-size query is fence-safe to read
    // (NUM_FRAMES_IN_FLIGHT recordCompaction calls later), each BLAS is copied into a right-sized buffer
    // (vkCmdCopyAccelerationStructureKHR eCompact), its address entries are repatched (the per-frame
    // TLAS rebuild picks the new BLAS up the same frame), and the original retires after another
    // frames-in-flight window. BLASes rebuilt or evicted in the meantime are skipped by handle compare.
    // Call once per frame, after the build step and before the TLAS build.
    void recordCompaction(vk::CommandBuffer cmd);

    uint64 getBlasTotalBytes() const; // current static BLAS memory (excludes skinned + retiring originals)
    uint64 getCompactionSavedBytes() const { return m_compactionSavedBytes; }

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
        bool compacted = false; // right-sized already (or not worth it); reset on rebuild
    };
    std::vector<Blas> m_blasList;

    // Compaction pipeline state: one query batch per build event, matured by recordCompaction-call
    // count (the frame-slot fence guarantees the queries executed by then); originals retire the same way.
    struct CompactionBatch
    {
        vk::QueryPool queryPool;
        std::vector<uint32> meshIndices;
        std::vector<vk::AccelerationStructureKHR> handles; // as recorded; mismatch = rebuilt/evicted, skip
        uint32 frameStamp;
    };
    std::deque<CompactionBatch> m_compactionBatches;
    struct RetiredBlas
    {
        vk::AccelerationStructureKHR handle;
        Buffer buffer;
        uint32 frameStamp;
    };
    std::vector<RetiredBlas> m_retiredBlas;
    uint64 m_compactionSavedBytes = 0;
    uint32 m_compactionFrame = 0;
    Buffer m_blasScratch;
    vk::DeviceAddress m_blasScratchAlignedAddr = 0;
    // Per frame in flight (skinned BLAS addresses differ between slots; static addresses are written to all).
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_blasAddressBuffers;
    std::array<std::span<uint64>, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_mappedBlasAddresses;
    uint32 m_numBlas = 0;

    // mesh idx -> mesh idx whose BLAS it uses (identity = owns one). Single-buffered: entries are static
    // per mesh and written before the mesh can first be instanced.
    Buffer m_meshAliasBuffer;
    std::span<uint32> m_mappedMeshAlias;
    uint32 m_numAliasMeshes = 0;

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
