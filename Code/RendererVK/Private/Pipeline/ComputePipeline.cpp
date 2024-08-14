module RendererVK.ComputePipeline;

import RendererVK.Device;
import RendererVK.Shader;

bool ComputePipeline::initialize(const ComputePipelineLayout& layout)
{
    vk::Device vkDevice = Globals::device.getDevice();

    vk::DescriptorSetLayoutCreateInfo layoutInfo
    {
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = (uint32)layout.descriptorSetLayoutBindings.size(),
        .pBindings = layout.descriptorSetLayoutBindings.data(),
    };
    m_descriptorSetLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo
    {
        .flags = {},
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorSetLayout,
        .pushConstantRangeCount = (uint32)layout.pushConstantRanges.size(),
        .pPushConstantRanges = layout.pushConstantRanges.data(),
    };
    m_pipelineLayout = vkDevice.createPipelineLayout(pipelineLayoutCreateInfo);

    Shader computeShader;
    computeShader.initialize(vk::ShaderStageFlagBits::eCompute, layout.computeShaderText);

    vk::SpecializationInfo specializationInfo
    {
        .mapEntryCount = 0,
        .pMapEntries = nullptr,
        .dataSize = 0,
        .pData = nullptr,
    };
    vk::ComputePipelineCreateInfo pipelineCreateInfo
    {
        .flags = {},
        .stage =
        {
            .stage = vk::ShaderStageFlagBits::eCompute,
            .module = computeShader.getModule(),
            .pName = "main",
            .pSpecializationInfo = &specializationInfo,
        },
        .layout = m_pipelineLayout,
        .basePipelineHandle = nullptr,
        .basePipelineIndex = -1,
    };

    m_pipelineCache = vkDevice.createPipelineCache(vk::PipelineCacheCreateInfo());
    m_pipeline = Globals::device.getDevice().createComputePipeline(m_pipelineCache, pipelineCreateInfo).value;

    return true;
}