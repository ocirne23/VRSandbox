module RendererVK:GIProbePipeline;

import Core;
import Core.glm;
import File.FileSystem;
import :Device;
import :TextureManager;
import :Texture;
import :Layout;

namespace
{
    struct TlasInstancePC { uint32 numInstances; };
    struct AllocPC { glm::ivec3 regionMin; uint32 regionDim; };
    struct TracePC
    {
        glm::ivec3 regionMin;
        uint32 regionDim;
        uint32 frameIndex;
        uint32 numRays;
        float temporalAlpha;
        float maxRayDist;
        float skyIntensity;
        float _pad0, _pad1, _pad2;
    };

    vk::DescriptorSetLayoutBinding storageBinding(uint32 b)
    {
        return vk::DescriptorSetLayoutBinding{ .binding = b, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute };
    }
}

void GIProbePipeline::initialize()
{
    m_textureSampler.initialize();

    m_probeTable.initialize(RendererVKLayout::GI_PROBE_TABLE_BUFFER_SIZE,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_probeGridData.initialize(RendererVKLayout::GI_PROBE_GRIDDATA_BUFFER_SIZE,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_probeSh.initialize(RendererVKLayout::GI_PROBE_SH_BUFFER_SIZE,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
    m_probeMeta.initialize(RendererVKLayout::GI_PROBE_META_BUFFER_SIZE,
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible);
    m_tlasInstanceBuffer.initialize((vk::DeviceSize)RendererVKLayout::GI_MAX_TLAS_INSTANCES * RendererVKLayout::GI_TLAS_INSTANCE_SIZE,
        vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    ComputePipelineLayout tlasLayout; buildTlasInstanceLayout(tlasLayout); m_tlasInstancePipeline.initialize(tlasLayout);
    ComputePipelineLayout allocLayout; buildAllocLayout(allocLayout); m_allocPipeline.initialize(allocLayout);
    ComputePipelineLayout traceLayout; buildTraceLayout(traceLayout); m_tracePipeline.initialize(traceLayout);

    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
    {
        m_tlasInstanceSets[i].initialize(m_tlasInstancePipeline.getDescriptorSetLayout());
        m_allocSets[i].initialize(m_allocPipeline.getDescriptorSetLayout());
        m_traceSets[i].initialize(m_tracePipeline.getDescriptorSetLayout());
    }
}

void GIProbePipeline::reloadShaders()
{
    ComputePipelineLayout tlasLayout; buildTlasInstanceLayout(tlasLayout);
    ComputePipelineLayout allocLayout; buildAllocLayout(allocLayout);
    ComputePipelineLayout traceLayout; buildTraceLayout(traceLayout);
    bool ok = m_tlasInstancePipeline.reloadShaders(tlasLayout);
    ok = m_allocPipeline.reloadShaders(allocLayout) && ok;
    ok = m_tracePipeline.reloadShaders(traceLayout) && ok;
    if (!ok)
        printf("GIProbePipeline: shader reload failed, keeping previous pipeline(s)\n");
}

void GIProbePipeline::buildTlasInstanceLayout(ComputePipelineLayout& layout)
{
    layout.computeShaderDebugFilePath = "Shaders/gi_tlas_instances.cs.glsl";
    layout.computeShaderText = FileSystem::readFileStr(layout.computeShaderDebugFilePath);
    for (uint32 b = 0; b <= 4; ++b)
        layout.descriptorSetLayoutBindings.push_back(storageBinding(b));
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(TlasInstancePC) });
}

void GIProbePipeline::buildAllocLayout(ComputePipelineLayout& layout)
{
    layout.computeShaderDebugFilePath = "Shaders/gi_probe_alloc.cs.glsl";
    layout.computeShaderText = FileSystem::readFileStr(layout.computeShaderDebugFilePath);
    for (uint32 b = 0; b <= 2; ++b)
        layout.descriptorSetLayoutBindings.push_back(storageBinding(b));
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(AllocPC) });
}

void GIProbePipeline::buildTraceLayout(ComputePipelineLayout& layout)
{
    layout.computeShaderDebugFilePath = "Shaders/gi_probe_trace.cs.glsl";
    layout.computeShaderText = FileSystem::readFileStr(layout.computeShaderDebugFilePath);
    auto& b = layout.descriptorSetLayoutBindings;
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute }); // UBO
    b.push_back(storageBinding(1)); // lightInfos
    b.push_back(storageBinding(2)); // lightGrid
    b.push_back(storageBinding(3)); // lightTable
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 4, .descriptorType = vk::DescriptorType::eAccelerationStructureKHR, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute }); // TLAS
    b.push_back(storageBinding(5)); // vertices
    b.push_back(storageBinding(6)); // indices
    b.push_back(storageBinding(7)); // meshInfos
    b.push_back(storageBinding(8)); // meshInstances
    b.push_back(storageBinding(9)); // materials
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 10, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = RendererVKLayout::MAX_TEXTURES, .stageFlags = vk::ShaderStageFlagBits::eCompute }); // textures
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 11, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute }); // shadow map
    b.push_back(storageBinding(12)); // probe table
    b.push_back(storageBinding(13)); // probe grid data
    b.push_back(storageBinding(14)); // probe SH
    b.push_back(storageBinding(15)); // probe meta
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(TracePC) });
}

void GIProbePipeline::recordClearPersistent(CommandBuffer& commandBuffer)
{
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    cmd.fillBuffer(m_probeTable.getBuffer(), 0, vk::WholeSize, 0xFFFFFFFF); // EMPTY_ENTRY
    cmd.fillBuffer(m_probeMeta.getBuffer(), 0, vk::WholeSize, 0);
    cmd.fillBuffer(m_probeGridData.getBuffer(), 0, vk::WholeSize, 0);
    cmd.fillBuffer(m_probeSh.getBuffer(), 0, vk::WholeSize, 0);

    vk::MemoryBarrier2 bar{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
}

void GIProbePipeline::recordTlasInstances(CommandBuffer& commandBuffer, uint32 frameIdx, TlasInstanceParams& params)
{
    if (params.numInstances == 0)
        return;
    DescriptorSet& set = m_tlasInstanceSets[frameIdx];
    vk::DescriptorSet vkSet = set.getDescriptorSet();

    auto bufInfo = [](Buffer& buf) { return vk::DescriptorBufferInfo{ .buffer = buf.getBuffer(), .range = buf.getSize() }; };
    std::array<DescriptorSetUpdateInfo, 5> updates{
        DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.renderNodeTransforms) } },
        DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.meshInstances) } },
        DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.instanceOffsets) } },
        DescriptorSetUpdateInfo{ .binding = 3, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.blasAddresses) } },
        DescriptorSetUpdateInfo{ .binding = 4, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_tlasInstanceBuffer) } },
    };
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_tlasInstancePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, vkSet, updates);
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_tlasInstancePipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_tlasInstancePipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);
    TlasInstancePC pc{ .numInstances = params.numInstances };
    cmd.pushConstants(m_tlasInstancePipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
    cmd.dispatch((params.numInstances + 63) / 64, 1, 1);
}

void GIProbePipeline::recordAlloc(CommandBuffer& commandBuffer, uint32 frameIdx, glm::ivec3 regionMin, uint32 regionDim)
{
    DescriptorSet& set = m_allocSets[frameIdx];
    vk::DescriptorSet vkSet = set.getDescriptorSet();
    auto bufInfo = [](Buffer& buf) { return vk::DescriptorBufferInfo{ .buffer = buf.getBuffer(), .range = buf.getSize() }; };
    std::array<DescriptorSetUpdateInfo, 3> updates{
        DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeTable) } },
        DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeGridData) } },
        DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeMeta) } },
    };
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_allocPipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, vkSet, updates);
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_allocPipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_allocPipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);
    AllocPC pc{ .regionMin = regionMin, .regionDim = regionDim };
    cmd.pushConstants(m_allocPipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
    const uint32 numCells = regionDim * regionDim * regionDim;
    cmd.dispatch((numCells + 63) / 64, 1, 1);
}

void GIProbePipeline::recordTrace(CommandBuffer& commandBuffer, uint32 frameIdx, TraceParams& params)
{
    DescriptorSet& set = m_traceSets[frameIdx];
    vk::DescriptorSet vkSet = set.getDescriptorSet();
    auto bufInfo = [](Buffer& buf) { return vk::DescriptorBufferInfo{ .buffer = buf.getBuffer(), .range = buf.getSize() }; };

    std::vector<DescriptorSetUpdateInfo> updates;
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer, .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.ubo.getBuffer(), .range = sizeof(RendererVKLayout::Ubo) } } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.lightInfos) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.lightGrid) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 3, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.lightTable) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 5, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.vertexBuffer) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 6, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.indexBuffer) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 7, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.meshInfos) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 8, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.meshInstances) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 9, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(params.materialInfos) } });

    DescriptorSetUpdateInfo texUpdate{ .binding = 10, .type = vk::DescriptorType::eCombinedImageSampler };
    const std::vector<Texture>& textures = Globals::textureManager.getTextures();
    for (const Texture& tex : textures)
        texUpdate.imageInfos.push_back(vk::DescriptorImageInfo{ .sampler = m_textureSampler.getSampler(), .imageView = tex.getImageView(), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal });
    if (!texUpdate.imageInfos.empty())
        updates.push_back(std::move(texUpdate));

    updates.push_back(DescriptorSetUpdateInfo{ .binding = 11, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { vk::DescriptorImageInfo{ .sampler = params.shadowMapSampler, .imageView = params.shadowMapView, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal } } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 12, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeTable) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 13, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeGridData) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 14, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeSh) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 15, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_probeMeta) } });

    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_tracePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, vkSet, updates);

    // The acceleration-structure descriptor (binding 4) needs a pNext'd write, which the buffer/image
    // helper above does not support; write it directly.
    vk::WriteDescriptorSetAccelerationStructureKHR asInfo{ .accelerationStructureCount = 1, .pAccelerationStructures = &params.tlas };
    vk::WriteDescriptorSet asWrite{ .pNext = &asInfo, .dstSet = vkSet, .dstBinding = 4, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eAccelerationStructureKHR };
    Globals::device.getDevice().updateDescriptorSets(1, &asWrite, 0, nullptr);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_tracePipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_tracePipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

    TracePC pc{
        .regionMin = params.regionMin,
        .regionDim = params.regionDim,
        .frameIndex = params.frameIndex,
        .numRays = RendererVKLayout::GI_RAYS_PER_PROBE,
        .temporalAlpha = RendererVKLayout::GI_TEMPORAL_ALPHA,
        .maxRayDist = RendererVKLayout::GI_MAX_RAY_DIST,
        .skyIntensity = RendererVKLayout::GI_SKY_INTENSITY,
    };
    cmd.pushConstants(m_tracePipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

    const uint32 totalProbes = params.regionDim * params.regionDim * params.regionDim * RendererVKLayout::GI_PROBES_PER_GRID;
    cmd.dispatch((totalProbes + 63) / 64, 1, 1);
}
