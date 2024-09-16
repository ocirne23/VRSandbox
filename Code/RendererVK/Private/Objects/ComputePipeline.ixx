export module RendererVK.ComputePipeline;

import Core;
import RendererVK.VK;

export struct ComputePipelineLayout
{
    std::string computeShaderText;
    std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    std::vector<vk::PushConstantRange> pushConstantRanges;
};

export class ComputePipeline final
{
public:
    ComputePipeline() {};
    ~ComputePipeline() {};
    ComputePipeline(const ComputePipeline&) = delete;

    bool initialize(const ComputePipelineLayout& layout);

    vk::Pipeline getPipeline() const { return m_pipeline; }
    vk::PipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }

private:

    vk::Pipeline m_pipeline;
    vk::PipelineCache m_pipelineCache;
    vk::PipelineLayout m_pipelineLayout;
    vk::DescriptorSetLayout m_descriptorSetLayout;
};