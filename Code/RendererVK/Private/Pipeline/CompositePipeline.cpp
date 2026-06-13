module RendererVK:CompositePipeline;

import Core;
import File.FileSystem;

import :CommandBuffer;
import :Device;

void CompositePipeline::buildPipelineLayout(GraphicsPipelineLayout& layout)
{
    layout.vertexShader.debugFilePath = "Shaders/composite.vs.glsl";
    layout.fragmentShader.debugFilePath = "Shaders/composite.fs.glsl";
    layout.vertexShader.text = FileSystem::readFileStr(layout.vertexShader.debugFilePath);
    layout.fragmentShader.text = FileSystem::readFileStr(layout.fragmentShader.debugFilePath);
    layout.cullMode = vk::CullModeFlagBits::eNone;
    layout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
        .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment });
    layout.descriptorSetLayoutBindings.push_back(vk::DescriptorSetLayoutBinding{
        .binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment });
    layout.pushConstantRanges.push_back(vk::PushConstantRange{
        .stageFlags = vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(CompositePC) });
}

void CompositePipeline::initialize(const RenderPass& renderPass)
{
    GraphicsPipelineLayout layout;
    buildPipelineLayout(layout);
    m_graphicsPipeline.initialize(renderPass, layout);
}

void CompositePipeline::reloadShaders(const RenderPass& renderPass)
{
    GraphicsPipelineLayout layout;
    buildPipelineLayout(layout);
    if (!m_graphicsPipeline.reloadShaders(renderPass, layout))
        printf("CompositePipeline: shader reload failed, keeping previous pipeline\n");
}

void CompositePipeline::record(CommandBuffer& commandBuffer, const RecordParams& params)
{
    vk::DescriptorSet descriptorSet = params.descriptorSet.getDescriptorSet();
    std::array<DescriptorSetUpdateInfo, 2> updates{
        DescriptorSetUpdateInfo{
            .binding = 0,
            .type = vk::DescriptorType::eCombinedImageSampler,
            .imageInfos = {
                vk::DescriptorImageInfo{
                    .sampler = params.sampler,
                    .imageView = params.resolvedView,
                    .imageLayout = vk::ImageLayout::eGeneral
    } } },
        DescriptorSetUpdateInfo{
            .binding = 1,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.exposureBuffer, .range = vk::WholeSize } } } };

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, descriptorSet, updates);
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
    vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);
    const CompositePC pc{ .exposure = exp2f(params.exposureEV), .tonemapper = params.tonemapper, .autoExposure = params.autoExposure };
    vkCommandBuffer.pushConstants(m_graphicsPipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, sizeof(pc), &pc);
    vkCommandBuffer.draw(3, 1, 0, 0);
}
