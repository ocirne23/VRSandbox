module RendererVK;

import Core;
import Core.glm;
import File;
import :Device;
import :Layout;

SkinningComputePipeline::SkinningComputePipeline() {}
SkinningComputePipeline::~SkinningComputePipeline() {}

void SkinningComputePipeline::initialize(uint32 maxPaletteEntries)
{
    resizePaletteBuffer(maxPaletteEntries);

    ComputePipelineLayout computePipelineLayout;
    buildComputeLayout(computePipelineLayout);
    m_computePipeline.initialize(computePipelineLayout);
}

void SkinningComputePipeline::resizePaletteBuffer(uint32 maxPaletteEntries)
{
    m_maxPaletteEntries = maxPaletteEntries;
    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.paletteBuffer.initialize(maxPaletteEntries * sizeof(glm::mat4),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "SkinningPalettes", BufferHostAccess::eSequentialWrite);
        perFrame.mappedPalettes = perFrame.paletteBuffer.mapMemory<glm::mat4>();
    }
}

void SkinningComputePipeline::reloadShaders()
{
    ComputePipelineLayout computePipelineLayout;
    buildComputeLayout(computePipelineLayout);
    if (!m_computePipeline.reloadShaders(computePipelineLayout))
        printf("SkinningComputePipeline: shader reload failed, keeping previous pipeline\n");
}

void SkinningComputePipeline::buildComputeLayout(ComputePipelineLayout& computePipelineLayout)
{
    computePipelineLayout.computeShaderDebugFilePath = "Shaders/skinning.cs.glsl";
    computePipelineLayout.computeShaderText = FileSystem::readFileStr(computePipelineLayout.computeShaderDebugFilePath);
    auto& bindings = computePipelineLayout.descriptorSetLayoutBindings;
    bindings.push_back(vk::DescriptorSetLayoutBinding{ // vertex buffer (read base, write output)
        .binding = 0, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    bindings.push_back(vk::DescriptorSetLayoutBinding{ // skinning influences
        .binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    bindings.push_back(vk::DescriptorSetLayoutBinding{ // bone palette
        .binding = 2, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });

    computePipelineLayout.pushConstantRanges.push_back(vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(RendererVKLayout::SkinningPushConstants) });
}

void SkinningComputePipeline::update(uint32 frameIdx, std::span<const glm::mat4> palettes)
{
    if (palettes.empty())
        return;
    PerFrameData& frameData = m_perFrameData[frameIdx];
    assert(palettes.size() <= frameData.mappedPalettes.size());
    memcpy(frameData.mappedPalettes.data(), palettes.data(), palettes.size() * sizeof(glm::mat4));
    frameData.paletteBuffer.flushMappedMemory(palettes.size() * sizeof(glm::mat4));
}

void SkinningComputePipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, RecordParams& params)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];

    std::array<DescriptorSetUpdateInfo, 3> updates
    {
        DescriptorSetUpdateInfo {
            .binding = 0, .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.vertexBuffer.getBuffer(), .range = params.vertexBuffer.getSize() } } },
        DescriptorSetUpdateInfo {
            .binding = 1, .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.skinningBuffer.getBuffer(), .range = params.skinningBuffer.getSize() } } },
        DescriptorSetUpdateInfo {
            .binding = 2, .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = frameData.paletteBuffer.getBuffer(), .range = frameData.paletteBuffer.getSize() } } },
    };

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    vk::DescriptorSet descriptorSet = params.descriptorSet.getDescriptorSet();
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipeline());
    commandBuffer.cmdUpdateDescriptorSets(m_computePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, descriptorSet, updates);
    vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);

    for (const RendererVKLayout::SkinningPushConstants& job : params.jobs)
    {
        vkCommandBuffer.pushConstants(m_computePipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(job), &job);
        const uint32 groups = (job.vertexCount + RendererVKLayout::SKINNING_THREADS_PER_GROUP - 1) / RendererVKLayout::SKINNING_THREADS_PER_GROUP;
        vkCommandBuffer.dispatch(groups, 1, 1);
    }

    // Skinned vertices are consumed as vertex input by the G-buffer / forward / shadow passes.
    vk::MemoryBarrier2 memoryBarrier{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eVertexAttributeInput | vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eShaderStorageRead,
    };
    vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &memoryBarrier });
}
