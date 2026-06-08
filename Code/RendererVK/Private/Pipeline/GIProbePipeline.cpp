module RendererVK:GIProbePipeline;

import Core;
import Core.glm;
import File.FileSystem;
import :Device;
import :TextureManager;
import :Texture;
import :GraphicsPipeline;
import :RenderPass;
import :Layout;

namespace
{
    struct TlasInstancePC { uint32 numInstances; };
    struct AllocPC
    {
        glm::ivec3 regionMin;
        uint32     regionDim;
        glm::vec3  viewPos;
        float      _pad;
    };
    struct TracePC
    {
        uint32 frameIndex;
        uint32 numRays;
        float temporalAlpha;
        float maxRayDist;
        float skyIntensity;
        float sunAngularCos;
        float sunGlow;
        float _pad0;
        glm::vec3 skyZenith;  float _pad1;
        glm::vec3 skyHorizon; float _pad2;
        glm::vec3 skyGround;  float _pad3;
        glm::vec3 skyUp;      float _pad4;
    };
    struct DebugPC
    {
        float  radius; // cube half-extent as a fraction of cell size
        uint32 mode;   // 0 = irradiance, 1 = cellSize/LOD color
    };

    constexpr uint32 GI_TABLE_EMPTY_ENTRY = 0xFFFFFFFFu;

    vk::DescriptorSetLayoutBinding storageBinding(uint32 b)
    {
        return vk::DescriptorSetLayoutBinding{ .binding = b, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute };
    }

    // Clear a probe grid table + work list to the empty/initial state.
    void clearGridTable(vk::CommandBuffer cmd, Buffer& table, Buffer& gridList)
    {
        cmd.fillBuffer(table.getBuffer(), 0, 8, 0);                                        // numGrids, gridCounter
        cmd.fillBuffer(table.getBuffer(), 8, 4, RendererVKLayout::GI_TABLE_NUM_ENTRIES);   // tableSize
        cmd.fillBuffer(table.getBuffer(), 12, vk::WholeSize, GI_TABLE_EMPTY_ENTRY);        // table entries
        cmd.fillBuffer(gridList.getBuffer(), 0, 4, 0);                                     // gridListCount
    }
}

void GIProbePipeline::initialize()
{
    m_textureSampler.initialize();

    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
    {
        m_giTable[i].initialize(RendererVKLayout::GI_TABLE_BUFFER_SIZE,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_giGridData[i].initialize(RendererVKLayout::GI_GRID_DATA_BUFFER_SIZE,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
        m_giGridList[i].initialize(RendererVKLayout::GI_GRID_LIST_BUFFER_SIZE,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
    }

    for (Buffer& instBuf : m_tlasInstanceBuffer)
        instBuf.initialize((vk::DeviceSize)RendererVKLayout::GI_MAX_TLAS_INSTANCES * RendererVKLayout::GI_TLAS_INSTANCE_SIZE,
            vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

    ComputePipelineLayout tlasLayout;  buildTlasInstanceLayout(tlasLayout); m_tlasInstancePipeline.initialize(tlasLayout);
    ComputePipelineLayout allocLayout; buildAllocLayout(allocLayout);       m_allocPipeline.initialize(allocLayout);
    ComputePipelineLayout traceLayout; buildTraceLayout(traceLayout);       m_tracePipeline.initialize(traceLayout);

    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
    {
        m_tlasInstanceSets[i].initialize(m_tlasInstancePipeline.getDescriptorSetLayout());
        m_allocSets[i].initialize(m_allocPipeline.getDescriptorSetLayout());
        m_traceSets[i].initialize(m_tracePipeline.getDescriptorSetLayout());
    }
}

void GIProbePipeline::reloadShaders()
{
    ComputePipelineLayout tlasLayout;  buildTlasInstanceLayout(tlasLayout);
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
    for (uint32 b = 0; b <= 4; ++b) // cur table, cur gridData, cur gridList, prev table, prev gridData
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
    b.push_back(storageBinding(12)); // cur gridData (SH)
    b.push_back(storageBinding(13)); // cur table
    b.push_back(storageBinding(14)); // cur gridList
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(TracePC) });
}

void GIProbePipeline::recordClearPersistent(CommandBuffer& commandBuffer)
{
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
        clearGridTable(cmd, m_giTable[i], m_giGridList[i]);

    vk::MemoryBarrier2 bar{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
}

void GIProbePipeline::recordClearCur(CommandBuffer& commandBuffer, uint32 frameIdx)
{
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    clearGridTable(cmd, m_giTable[frameIdx], m_giGridList[frameIdx]);

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
        DescriptorSetUpdateInfo{ .binding = 4, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_tlasInstanceBuffer[frameIdx]) } },
    };
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_tlasInstancePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, vkSet, updates);
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_tlasInstancePipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_tlasInstancePipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);
    TlasInstancePC pc{ .numInstances = params.numInstances };
    cmd.pushConstants(m_tlasInstancePipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
    cmd.dispatch((params.numInstances + 63) / 64, 1, 1);
}

void GIProbePipeline::recordAlloc(CommandBuffer& commandBuffer, uint32 frameIdx, const AllocParams& params)
{
    recordClearCur(commandBuffer, frameIdx);

    const uint32 prevIdx = (frameIdx + 1) % RendererVKLayout::NUM_FRAMES_IN_FLIGHT;
    DescriptorSet& set = m_allocSets[frameIdx];
    vk::DescriptorSet vkSet = set.getDescriptorSet();
    auto bufInfo = [](Buffer& buf) { return vk::DescriptorBufferInfo{ .buffer = buf.getBuffer(), .range = buf.getSize() }; };

    std::array<DescriptorSetUpdateInfo, 5> updates{
        DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giTable[frameIdx]) } },
        DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridData[frameIdx]) } },
        DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridList[frameIdx]) } },
        DescriptorSetUpdateInfo{ .binding = 3, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giTable[prevIdx]) } },
        DescriptorSetUpdateInfo{ .binding = 4, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridData[prevIdx]) } },
    };

    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_allocPipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, vkSet, updates);
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_allocPipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_allocPipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);
    AllocPC pc{ .regionMin = params.regionMin, .regionDim = params.regionDim, .viewPos = params.viewPos, ._pad = 0.0f };
    cmd.pushConstants(m_allocPipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
    const uint32 numCubes = params.regionDim * params.regionDim * params.regionDim;
    cmd.dispatch((numCubes + 63) / 64, 1, 1);
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
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 12, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridData[frameIdx]) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 13, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giTable[frameIdx]) } });
    updates.push_back(DescriptorSetUpdateInfo{ .binding = 14, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridList[frameIdx]) } });

    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_tracePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, vkSet, updates);

    // The acceleration-structure descriptor (binding 4) needs a pNext'd write the buffer/image helper
    // does not support; write it directly.
    vk::WriteDescriptorSetAccelerationStructureKHR asInfo{ .accelerationStructureCount = 1, .pAccelerationStructures = &params.tlas };
    vk::WriteDescriptorSet asWrite{ .pNext = &asInfo, .dstSet = vkSet, .dstBinding = 4, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eAccelerationStructureKHR };
    Globals::device.getDevice().updateDescriptorSets(1, &asWrite, 0, nullptr);

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_tracePipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_tracePipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);

    // Trace tuning (passed via push constants). Rays are amortized over frames via the temporal blend.
    constexpr uint32 GI_RAYS_PER_PROBE = 17;
    constexpr float  GI_TEMPORAL_ALPHA = 0.005f;
    constexpr float  GI_MAX_RAY_DIST = 16.0f;

    TracePC pc{
        .frameIndex = params.frameIndex,
        .numRays = GI_RAYS_PER_PROBE,
        .temporalAlpha = GI_TEMPORAL_ALPHA,
        .maxRayDist = GI_MAX_RAY_DIST,
    };
    cmd.pushConstants(m_tracePipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);

    cmd.dispatch((RendererVKLayout::GI_TRACE_THREADS + 63) / 64, 1, 1);
}

void GIProbePipeline::buildDebugLayout(GraphicsPipelineLayout& layout)
{
    layout.vertexShader.debugFilePath = "Shaders/gi_probe_debug.vs.glsl";
    layout.fragmentShader.debugFilePath = "Shaders/gi_probe_debug.fs.glsl";
    layout.vertexShader.text = FileSystem::readFileStr(layout.vertexShader.debugFilePath);
    layout.fragmentShader.text = FileSystem::readFileStr(layout.fragmentShader.debugFilePath);
    layout.cullMode = vk::CullModeFlagBits::eNone; // procedural cube, winding not guaranteed

    auto& b = layout.descriptorSetLayoutBindings;
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex }); // UBO (mvp)
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex }); // gridData
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 2, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex }); // table
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 3, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex }); // gridList
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eVertex, .offset = 0, .size = sizeof(DebugPC) });
}

void GIProbePipeline::initializeDebug(const RenderPass& renderPass)
{
    m_debugRenderPass = &renderPass;
    GraphicsPipelineLayout layout; buildDebugLayout(layout);
    m_debugPipeline.initialize(renderPass, layout);
    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
        m_debugSets[i].initialize(m_debugPipeline.getDescriptorSetLayout());
}

void GIProbePipeline::reloadDebugShaders()
{
    if (!m_debugRenderPass)
        return;
    GraphicsPipelineLayout layout; buildDebugLayout(layout);
    if (!m_debugPipeline.reloadShaders(*m_debugRenderPass, layout))
        printf("GIProbePipeline: debug shader reload failed, keeping previous pipeline\n");
}

void GIProbePipeline::recordDebugDraw(CommandBuffer& commandBuffer, uint32 frameIdx, Buffer& ubo, float radius, uint32 mode)
{
    DescriptorSet& set = m_debugSets[frameIdx];
    vk::DescriptorSet vkSet = set.getDescriptorSet();
    auto bufInfo = [](Buffer& buf) { return vk::DescriptorBufferInfo{ .buffer = buf.getBuffer(), .range = buf.getSize() }; };

    std::array<DescriptorSetUpdateInfo, 4> updates{
        DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer, .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = ubo.getBuffer(), .range = sizeof(RendererVKLayout::Ubo) } } },
        DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridData[frameIdx]) } },
        DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giTable[frameIdx]) } },
        DescriptorSetUpdateInfo{ .binding = 3, .type = vk::DescriptorType::eStorageBuffer, .bufferInfos = { bufInfo(m_giGridList[frameIdx]) } },
    };

    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_debugPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, vkSet, updates);
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, m_debugPipeline.getPipeline());
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_debugPipeline.getPipelineLayout(), 0, 1, &vkSet, 0, nullptr);
    DebugPC pc{ .radius = radius, .mode = mode };
    cmd.pushConstants(m_debugPipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex, 0, sizeof(pc), &pc);
    // One instanced cube (36 verts) per probe-cell slot; out-of-range instances self-cull in the VS.
    cmd.draw(36, RendererVKLayout::GI_TRACE_THREADS, 0, 0);
}
