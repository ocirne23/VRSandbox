module RendererVK.ComputePipeline;

import RendererVK.Device;
import RendererVK.Shader;

ComputePipeline::~ComputePipeline()
{
    vk::Device vkDevice = Globals::device.getDevice();
    if (m_pipeline)
        vkDevice.destroyPipeline(m_pipeline);
    if (m_pipelineCache)
        vkDevice.destroyPipelineCache(m_pipelineCache);
    if (m_pipelineLayout)
        vkDevice.destroyPipelineLayout(m_pipelineLayout);
    if (m_descriptorSetLayout)
        vkDevice.destroyDescriptorSetLayout(m_descriptorSetLayout);
}

bool ComputePipeline::initialize(const ComputePipelineLayout& layout)
{
    vk::Device vkDevice = Globals::device.getDevice();

    std::vector<vk::DescriptorBindingFlags> layoutBindingFlags;
    layoutBindingFlags.reserve(layout.descriptorSetLayoutBindings.size());
    for (const auto& binding : layout.descriptorSetLayoutBindings)
    {
        vk::DescriptorBindingFlags flags = {};
        //if (layoutBindingFlags.size() + 1 == layout.descriptorSetLayoutBindings.size())
        //    flags |= vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
        layoutBindingFlags.push_back(flags);
    }
    vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo
    {
        .bindingCount = (uint32)layout.descriptorSetLayoutBindings.size(),
        .pBindingFlags = layoutBindingFlags.data(),
    };

    vk::DescriptorSetLayoutCreateInfo layoutInfo
    {
        .pNext = &bindingFlagsInfo,
        //.flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = (uint32)layout.descriptorSetLayoutBindings.size(),
        .pBindings = layout.descriptorSetLayoutBindings.data(),
    };

    auto layoutResult = vkDevice.createDescriptorSetLayout(layoutInfo);
    if (layoutResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create descriptor set layout");
        return false;
    }
    m_descriptorSetLayout = layoutResult.value;

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo
    {
        .flags = {},
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorSetLayout,
        .pushConstantRangeCount = (uint32)layout.pushConstantRanges.size(),
        .pPushConstantRanges = layout.pushConstantRanges.data(),
    };
    auto pipelineLayoutResult = vkDevice.createPipelineLayout(pipelineLayoutCreateInfo);
    if (pipelineLayoutResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create pipeline layout");
        return false;
    }
    m_pipelineLayout = pipelineLayoutResult.value;

    Shader computeShader;
    computeShader.initialize(vk::ShaderStageFlagBits::eCompute, layout.computeShaderText, layout.computeShaderDebugFilePath);

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
    auto pipelineCacheResult = vkDevice.createPipelineCache(vk::PipelineCacheCreateInfo());
    if (pipelineCacheResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create pipeline cache");
        return false;
    }
    m_pipelineCache = pipelineCacheResult.value;

    auto pipelineResult = vkDevice.createComputePipeline(m_pipelineCache, pipelineCreateInfo);
    if (pipelineResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create compute pipeline");
        return false;
    }
    m_pipeline = pipelineResult.value;
    return true;
}