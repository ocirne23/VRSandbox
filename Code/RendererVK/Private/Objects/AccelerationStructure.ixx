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

    void initialize();

    // Records BLAS builds for meshes [firstMesh, firstMesh+count) into cmd, reading geometry directly
    // from the shared vertex/index buffers. totalVertices is the current vertex-buffer fill (in vertices)
    // and is only used to derive a safe in-bounds maxVertex per mesh. BLASes are built once per mesh.
    void recordBuildBlas(vk::CommandBuffer cmd, Buffer& vertexBuffer, Buffer& indexBuffer,
        const RendererVKLayout::MeshInfo* meshInfos, uint32 firstMesh, uint32 count, uint32 totalVertices);

    // Records a TLAS (re)build from numInstances pre-written VkAccelerationStructureInstanceKHR records
    // in instanceBuffer. The instance buffer is produced by the TLAS-instance compute shader.
    void recordBuildTlas(vk::CommandBuffer cmd, Buffer& instanceBuffer, uint32 numInstances);

    // mesh idx -> BLAS device address (uint64), consumed by the TLAS-instance compute shader.
    Buffer& getBlasAddressBuffer() { return m_blasAddressBuffer; }
    uint32 getNumBlas() const { return m_numBlas; }

    vk::AccelerationStructureKHR getTlas() const { return m_tlas; }
    bool hasTlas() const { return (bool)m_tlas; }

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

    vk::AccelerationStructureKHR m_tlas = nullptr;
    Buffer m_tlasBuffer;
    Buffer m_tlasScratch;
    vk::DeviceAddress m_tlasScratchAlignedAddr = 0;
    uint32 m_tlasCapacity = 0;

    uint32 m_scratchAlignment = 256;
};
