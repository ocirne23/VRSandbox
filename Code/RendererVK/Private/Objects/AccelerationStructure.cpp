module RendererVK:AccelerationStructure;

import Core;
import :VK;
import :Device;

AccelerationStructure::AccelerationStructure() {}
AccelerationStructure::~AccelerationStructure()
{
    vk::Device dev = Globals::device.getDevice();
    for (Blas& blas : m_blasList)
        if (blas.handle)
            dev.destroyAccelerationStructureKHR(blas.handle);
    for (vk::AccelerationStructureKHR tlas : m_tlas)
        if (tlas)
            dev.destroyAccelerationStructureKHR(tlas);
}

void AccelerationStructure::initialize(uint32 maxUniqueMeshes)
{
    vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
    vk::PhysicalDeviceProperties2 props2{ .pNext = &asProps };
    Globals::device.getPhysicalDevice().getProperties2(&props2);
    m_scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;
    if (m_scratchAlignment == 0)
        m_scratchAlignment = 256;

    m_blasAddressBuffer.initialize(maxUniqueMeshes * sizeof(uint64),
        vk::BufferUsageFlagBits2::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
    m_mappedBlasAddresses = m_blasAddressBuffer.mapMemory<uint64>();
}

void AccelerationStructure::resizeBlasAddressBuffer(uint32 maxUniqueMeshes)
{
    const std::vector<uint64> oldAddresses(m_mappedBlasAddresses.begin(), m_mappedBlasAddresses.end());
    m_blasAddressBuffer.initialize(maxUniqueMeshes * sizeof(uint64),
        vk::BufferUsageFlagBits2::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
    m_mappedBlasAddresses = m_blasAddressBuffer.mapMemory<uint64>();
    memcpy(m_mappedBlasAddresses.data(), oldAddresses.data(), oldAddresses.size() * sizeof(uint64));
    m_blasAddressBuffer.flushMappedMemory(oldAddresses.size() * sizeof(uint64));
}

void AccelerationStructure::ensureScratch(Buffer& scratch, vk::DeviceAddress& outAlignedAddr, vk::DeviceSize needed)
{
    const vk::DeviceSize allocSize = needed + m_scratchAlignment;
    if (scratch.getSize() < allocSize)
    {
        scratch.initialize(allocSize,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    }
    const vk::DeviceAddress base = scratch.getDeviceAddress();
    const vk::DeviceAddress mask = (vk::DeviceAddress)m_scratchAlignment - 1;
    outAlignedAddr = (base + mask) & ~mask;
}

void AccelerationStructure::recordBuildBlas(vk::CommandBuffer cmd, Buffer& vertexBuffer, Buffer& indexBuffer,
    const RendererVKLayout::MeshInfo* meshInfos, uint32 firstMesh, uint32 count, uint32 totalVertices)
{
    if (count == 0)
        return;

    vk::Device dev = Globals::device.getDevice();
    const vk::DeviceAddress vbAddr = vertexBuffer.getDeviceAddress();
    const vk::DeviceAddress ibAddr = indexBuffer.getDeviceAddress();

    if (m_blasList.size() < firstMesh + count)
        m_blasList.resize(firstMesh + count);

    // Pass 1: assemble geometry + build infos, query sizes, find the largest scratch requirement.
    std::vector<vk::AccelerationStructureGeometryKHR> geoms(count);
    std::vector<vk::AccelerationStructureBuildGeometryInfoKHR> buildInfos(count);
    std::vector<vk::AccelerationStructureBuildRangeInfoKHR> ranges(count);
    std::vector<vk::AccelerationStructureBuildSizesInfoKHR> sizes(count);
    std::vector<vk::DeviceSize> scratchOffsets(count);
    vk::DeviceSize totalScratch = 0;
    const vk::DeviceSize align = m_scratchAlignment;
    for (uint32 i = 0; i < count; i++)
    {
        const RendererVKLayout::MeshInfo& mi = meshInfos[firstMesh + i];
        const uint32 primCount = mi.indexCount / 3;

        // This mesh's exact vertex count. Meshes are uploaded sequentially (monotonic vertexOffset), so a
        // mesh spans up to the next mesh's vertexOffset, and the batch's last mesh up to totalVertices.
        // maxVertex MUST be tight: declaring the whole buffer makes the driver bound the BLAS over every
        // vertex, ballooning its AABB to span the scene and degenerating the BVH (catastrophically slow
        // ray traversal -> TDR / device lost).
        const uint32 meshVertexCount = (i + 1u < count)
            ? (uint32)(meshInfos[firstMesh + i + 1u].vertexOffset - mi.vertexOffset)
            : (totalVertices - (uint32)mi.vertexOffset);
        // A zero-span mesh (two meshes sharing a vertexOffset, an empty mesh, or a trailing mesh where
        // totalVertices == vertexOffset) underflows maxVertex below to 0xFFFFFFFF, making the driver bound
        // this BLAS over the whole vertex buffer -> ballooning AABB and wild vertex fetches that MMU-fault
        // during ray traversal. Guard it.
        assert(meshVertexCount > 0 && "zero-span mesh -> maxVertex underflow");

        // vk::DeviceOrHostAddressConstKHR / AccelerationStructureGeometryDataKHR are unions, which can't
        // be designated-initialized; default-construct then assign the active union member.
        vk::AccelerationStructureGeometryTrianglesDataKHR tri{
            .vertexFormat = vk::Format::eR32G32B32Sfloat,
            .vertexStride = sizeof(RendererVKLayout::MeshVertex),
            .maxVertex = meshVertexCount - 1u,
            .indexType = vk::IndexType::eUint32,
        };
        tri.vertexData.deviceAddress = vbAddr;
        tri.indexData.deviceAddress = ibAddr;
        tri.transformData.deviceAddress = 0;

        geoms[i] = vk::AccelerationStructureGeometryKHR{
            .geometryType = vk::GeometryTypeKHR::eTriangles,
            // Opacity is decided per TLAS instance (ForceOpaque for everything except alpha-masked
            // materials, see gi_tlas_instances.cs.glsl), so the BLAS geometry must not be flagged opaque.
            .flags = {},
        };
        geoms[i].geometry.triangles = tri;
        buildInfos[i] = vk::AccelerationStructureBuildGeometryInfoKHR{
            .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
            .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
            .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
            .geometryCount = 1,
            .pGeometries = &geoms[i],
        };
        uint32 maxPrim = primCount;
        sizes[i] = dev.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfos[i], maxPrim);
        // Give every build its own scratch region (aligned) so concurrent builds never alias scratch
        // memory -- sharing one scratch region across builds is what was faulting the GPU.
        scratchOffsets[i] = totalScratch;
        totalScratch += (sizes[i].buildScratchSize + align - 1) & ~(align - 1);

        ranges[i] = vk::AccelerationStructureBuildRangeInfoKHR{
            .primitiveCount = primCount,
            .primitiveOffset = mi.firstIndex * (uint32)sizeof(RendererVKLayout::MeshIndex),
            .firstVertex = (uint32)mi.vertexOffset,
            .transformOffset = 0,
        };
    }

    ensureScratch(m_blasScratch, m_blasScratchAlignedAddr, totalScratch);

    // Pass 2: create and build each BLAS, serialized with a barrier between builds (each also uses a
    // disjoint scratch region). Serializing removes any concurrent-build interaction as a variable.
    for (uint32 i = 0; i < count; i++)
    {
        Blas& blas = m_blasList[firstMesh + i];
        if (blas.handle)
            dev.destroyAccelerationStructureKHR(blas.handle);
        blas.buffer.initialize(sizes[i].accelerationStructureSize,
            vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        vk::AccelerationStructureCreateInfoKHR ci{
            .buffer = blas.buffer.getBuffer(),
            .size = sizes[i].accelerationStructureSize,
            .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
        };
        auto res = dev.createAccelerationStructureKHR(ci);
        assert(res.result == vk::Result::eSuccess && "Failed to create BLAS");
        blas.handle = res.value;

        buildInfos[i].dstAccelerationStructure = blas.handle;
        buildInfos[i].scratchData.deviceAddress = m_blasScratchAlignedAddr + scratchOffsets[i];
        const vk::AccelerationStructureBuildRangeInfoKHR* pRange = &ranges[i];
        cmd.buildAccelerationStructuresKHR(buildInfos[i], pRange);

        vk::AccelerationStructureDeviceAddressInfoKHR ai{ .accelerationStructure = blas.handle };
        m_mappedBlasAddresses[firstMesh + i] = dev.getAccelerationStructureAddressKHR(ai);

        vk::MemoryBarrier2 b{
            .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR | vk::AccessFlagBits2::eAccelerationStructureReadKHR,
            .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR | vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &b });
    }

    // One barrier so all BLAS builds complete before the TLAS build (or any reads) consume them.
    vk::MemoryBarrier2 bar{
        .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        .dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });

    m_numBlas = std::max(m_numBlas, firstMesh + count);
    m_blasAddressBuffer.flushMappedMemory(m_blasAddressBuffer.getSize());
}

bool AccelerationStructure::recordBuildTlas(vk::CommandBuffer cmd, uint32 frameIdx, Buffer& instanceBuffer, uint32 numInstances)
{
    if (numInstances == 0)
        return false;

    vk::Device dev = Globals::device.getDevice();

    vk::AccelerationStructureGeometryInstancesDataKHR inst{ .arrayOfPointers = vk::False };
    inst.data.deviceAddress = instanceBuffer.getDeviceAddress();
    vk::AccelerationStructureGeometryKHR geom{
        .geometryType = vk::GeometryTypeKHR::eInstances,
        .flags = vk::GeometryFlagBitsKHR::eOpaque,
    };
    geom.geometry.instances = inst;
    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{
        .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
        .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
        .geometryCount = 1,
        .pGeometries = &geom,
    };

    // Grow (and recreate) the TLAS only when the instance count exceeds the current capacity; otherwise
    // rebuild in place. Capacity is rounded up generously so this is rare. Returns true when the handle
    // changed, so callers that bake it (e.g. the recorded-once AO pass) know to re-record.
    const bool handleChanged = (!m_tlas[frameIdx] || numInstances > m_tlasCapacity[frameIdx]);
    if (handleChanged)
    {
        const uint32 capacity = ((numInstances + 4095u) / 4096u) * 4096u;
        vk::AccelerationStructureBuildSizesInfoKHR sizes = dev.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, capacity);
        if (m_tlas[frameIdx])
            dev.destroyAccelerationStructureKHR(m_tlas[frameIdx]);
        m_tlasBuffer[frameIdx].initialize(sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits2::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::AccelerationStructureCreateInfoKHR ci{
            .buffer = m_tlasBuffer[frameIdx].getBuffer(),
            .size = sizes.accelerationStructureSize,
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        };
        auto res = dev.createAccelerationStructureKHR(ci);
        assert(res.result == vk::Result::eSuccess && "Failed to create TLAS");
        m_tlas[frameIdx] = res.value;
        ensureScratch(m_tlasScratch[frameIdx], m_tlasScratchAlignedAddr[frameIdx], sizes.buildScratchSize);
        m_tlasCapacity[frameIdx] = capacity;
    }

    buildInfo.dstAccelerationStructure = m_tlas[frameIdx];
    buildInfo.scratchData.deviceAddress = m_tlasScratchAlignedAddr[frameIdx];
    vk::AccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = numInstances,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };
    const vk::AccelerationStructureBuildRangeInfoKHR* pRange = &range;
    cmd.buildAccelerationStructuresKHR(buildInfo, pRange);
    return handleChanged;
}
