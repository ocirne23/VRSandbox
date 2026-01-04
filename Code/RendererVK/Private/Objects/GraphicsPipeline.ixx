export module RendererVK.GraphicsPipeline;
extern "C++" {

import Core;
import RendererVK.VK;
import RendererVK.Layout;

export class RenderPass;

export struct VertexLayoutInfo
{
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
};

export struct GraphicsPipelineLayout
{
    std::string fragmentShaderText;
    std::string fragmentShaderDebugFilePath;
    std::string vertexShaderText;
    std::string vertexShaderDebugFilePath;
    VertexLayoutInfo vertexLayoutInfo;
    std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    std::vector<vk::PushConstantRange> pushConstantRanges;
};

export class GraphicsPipeline final
{
public:
    GraphicsPipeline();
    ~GraphicsPipeline();
    GraphicsPipeline(const GraphicsPipeline&) = delete;

    bool initialize(const RenderPass& renderPass, GraphicsPipelineLayout& layout);

    vk::Pipeline getPipeline() const { return m_pipeline; }
    vk::PipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }

private:

    vk::Pipeline m_pipeline;
    vk::PipelineCache m_pipelineCache;
    vk::PipelineLayout m_pipelineLayout;
    vk::DescriptorSetLayout m_descriptorSetLayout;
};
} // extern "C++"