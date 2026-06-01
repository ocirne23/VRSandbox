export module RendererVK:GraphicsPipeline;

import Core;
import :VK;
import :Layout;

export class RenderPass;

export struct VertexLayoutInfo
{
    std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
    std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
};

export struct ShaderSource
{
    std::string text;
    std::string debugFilePath;
};

export struct PipelineVariant
{
    // Per-variant shader overrides; an empty text field falls back to the layout default.
    ShaderSource vertexShader;
    ShaderSource fragmentShader;
    // Per-variant pipeline state. Transparent variants enable alpha blending and disable depth
    // writes; opaque variants (the default) keep blending off and depth writes on.
    bool blendEnable = false;
    bool depthWrite = true;
};

export struct GraphicsPipelineLayout
{
    ShaderSource vertexShader;
    ShaderSource fragmentShader;
    VertexLayoutInfo vertexLayoutInfo;
    std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    std::vector<vk::PushConstantRange> pushConstantRanges;

    // Additional pipeline variants for DGC Indirect Execution Set selection. Variant 0 always
    // uses both layout defaults; each extra entry can independently override either shader stage.
    std::vector<PipelineVariant> additionalVariants;
};

export class GraphicsPipeline final
{
public:
    GraphicsPipeline();
    ~GraphicsPipeline();
    GraphicsPipeline(const GraphicsPipeline&) = delete;

    bool initialize(const RenderPass& renderPass, GraphicsPipelineLayout& layout);

    vk::Pipeline getPipeline() const { return m_pipelines[0]; }
    vk::Pipeline getPipelineVariant(uint32 variantIdx) const { return m_pipelines[variantIdx]; }
    uint32 getPipelineVariantCount() const { return (uint32)m_pipelines.size(); }
    vk::PipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }

private:

    std::vector<vk::Pipeline> m_pipelines;
    vk::PipelineCache m_pipelineCache;
    vk::PipelineLayout m_pipelineLayout;
    vk::DescriptorSetLayout m_descriptorSetLayout;
};