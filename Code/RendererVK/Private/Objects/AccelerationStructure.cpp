module RendererVK:AccelerationStructure;

import :VK;
import :Device;

AccelerationStructure::AccelerationStructure() {}
AccelerationStructure::~AccelerationStructure()
{
    vk::Device dev = Globals::device.getDevice();
    for (Blas& blas : m_blasList)
        if (blas.handle)
            dev.destroyAccelerationStructureKHR(blas.handle);
    if (m_tlas)
        dev.destroyAccelerationStructureKHR(m_tlas);
}

void AccelerationStructure::initialize()
{
    vk::PhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
    vk::PhysicalDeviceProperties2 props2{ .pNext = &asProps };
    Globals::device.getPhysicalDevice().getProperties2(&props2);
    m_scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;
    if (m_scratchAlignment == 0)
        m_scratchAlignment = 256;

    m_blasAddressBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(uint64),
        vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
    m_mappedBlasAddresses = m_blasAddressBuffer.mapMemory<uint64>();
}

void AccelerationStructure::ensureScratch(Buffer& scratch, vk::DeviceAddress& outAlignedAddr, vk::DeviceSize needed)
{
    const vk::DeviceSize allocSize = needed + m_scratchAlignment;
    if (scratch.getSize() < allocSize)
    {
        scratch.initialize(allocSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
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
        if (meshVertexCount == 0 || meshVertexCount > 4000000 || mi.indexCount == 0)
            printf("[GI BLAS] mesh %u SUSPECT: indexCount=%u vertexOffset=%d vertexCount=%u\n",
                firstMesh + i, mi.indexCount, mi.vertexOffset, meshVertexCount);

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
            .flags = vk::GeometryFlagBitsKHR::eOpaque,
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

    // Pass 2: create each BLAS and record its build. Each build writes a disjoint scratch region, so the
    // builds can run without inter-build barriers; a single barrier afterwards makes all BLASes readable
    // by the subsequent TLAS build.
    for (uint32 i = 0; i < count; i++)
    {
        Blas& blas = m_blasList[firstMesh + i];
        if (blas.handle)
            dev.destroyAccelerationStructureKHR(blas.handle);
        blas.buffer.initialize(sizes[i].accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
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
    m_blasAddressBuffer.flushMappedMemory(vk::WholeSize);
}

void AccelerationStructure::recordBuildTlas(vk::CommandBuffer cmd, Buffer& instanceBuffer, uint32 numInstances)
{
    if (numInstances == 0)
        return;

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
    // rebuild in place. Capacity is rounded up generously so this is rare.
    if (!m_tlas || numInstances > m_tlasCapacity)
    {
        const uint32 capacity = ((numInstances + 4095u) / 4096u) * 4096u;
        vk::AccelerationStructureBuildSizesInfoKHR sizes = dev.getAccelerationStructureBuildSizesKHR(vk::AccelerationStructureBuildTypeKHR::eDevice, buildInfo, capacity);
        if (m_tlas)
            dev.destroyAccelerationStructureKHR(m_tlas);
        m_tlasBuffer.initialize(sizes.accelerationStructureSize,
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::AccelerationStructureCreateInfoKHR ci{
            .buffer = m_tlasBuffer.getBuffer(),
            .size = sizes.accelerationStructureSize,
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
        };
        auto res = dev.createAccelerationStructureKHR(ci);
        assert(res.result == vk::Result::eSuccess && "Failed to create TLAS");
        m_tlas = res.value;
        ensureScratch(m_tlasScratch, m_tlasScratchAlignedAddr, sizes.buildScratchSize);
        m_tlasCapacity = capacity;
    }

    buildInfo.dstAccelerationStructure = m_tlas;
    buildInfo.scratchData.deviceAddress = m_tlasScratchAlignedAddr;
    vk::AccelerationStructureBuildRangeInfoKHR range{
        .primitiveCount = numInstances,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0,
    };
    const vk::AccelerationStructureBuildRangeInfoKHR* pRange = &range;
    cmd.buildAccelerationStructuresKHR(buildInfo, pRange);
}
