module RendererVK:GBufferPipeline;

import Core;
import File.FileSystem;

import :CommandBuffer;
import :Device;
import :Layout;

void GBufferPipeline::buildPipelineLayout(GraphicsPipelineLayout& layout)
{
    layout.vertexShader.debugFilePath = "Shaders/gbuffer.vs.glsl";
    layout.fragmentShader.debugFilePath = "Shaders/gbuffer.fs.glsl";
    layout.vertexShader.text = FileSystem::readFileStr(layout.vertexShader.debugFilePath);
    layout.fragmentShader.text = FileSystem::readFileStr(layout.fragmentShader.debugFilePath);
    layout.cullMode = vk::CullModeFlagBits::eBack;

    auto& bindings = layout.vertexLayoutInfo.bindingDescriptions;
    bindings.push_back(vk::VertexInputBindingDescription{ .binding = 0, .stride = sizeof(RendererVKLayout::MeshVertex), .inputRate = vk::VertexInputRate::eVertex });
    bindings.push_back(vk::VertexInputBindingDescription{ .binding = 2, .stride = sizeof(uint32), .inputRate = vk::VertexInputRate::eInstance });

    auto& attributes = layout.vertexLayoutInfo.attributeDescriptions;
    attributes.push_back(vk::VertexInputAttributeDescription{ .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(RendererVKLayout::MeshVertex, position) });
    attributes.push_back(vk::VertexInputAttributeDescription{ .location = 1, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(RendererVKLayout::MeshVertex, normal) });
    attributes.push_back(vk::VertexInputAttributeDescription{ .location = 4, .binding = 2, .format = vk::Format::eR32Uint, .offset = 0 });

    auto& descriptorSetBindings = layout.descriptorSetLayoutBindings;
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // UBO
        .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // transformed instances
        .binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex });
}

void GBufferPipeline::initialize(const GBuffer& gbuffer)
{
    GraphicsPipelineLayout layout;
    buildPipelineLayout(layout);
    m_graphicsPipeline.initialize(gbuffer.getRenderPass(), layout);
}

void GBufferPipeline::reloadShaders(const GBuffer& gbuffer)
{
    GraphicsPipelineLayout layout;
    buildPipelineLayout(layout);
    if (!m_graphicsPipeline.reloadShaders(gbuffer.getRenderPass(), layout))
        printf("GBufferPipeline: shader reload failed, keeping previous pipeline\n");
}

void GBufferPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params)
{
    std::array<DescriptorSetUpdateInfo, 2> updates{
        DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.ubo.getBuffer(), .range = sizeof(RendererVKLayout::Ubo) } } },
        DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.meshInstanceBuffer.getBuffer(), .range = params.meshInstanceBuffer.getSize() } } },
    };

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    vk::DescriptorSet descriptorSet = params.descriptorSet.getDescriptorSet();
    commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, descriptorSet, updates);
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
    vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);
    vkCommandBuffer.bindVertexBuffers(0, { params.vertexBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindVertexBuffers(2, { params.instanceIdxBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindIndexBuffer(params.indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);

    // Reuse the camera-cull IndirectDrawSequence buffer: skip the leading pipelineIndex uint (offset 4),
    // and draw one VkDrawIndexedIndirectCommand per mesh (stride = sizeof(IndirectDrawSequence) = 24).
    vkCommandBuffer.drawIndexedIndirect(params.indirectCommandBuffer.getBuffer(), 4, numMeshes, sizeof(RendererVKLayout::IndirectDrawSequence));
}
