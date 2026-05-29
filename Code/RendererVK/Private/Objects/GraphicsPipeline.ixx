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

export struct ShaderVariant
{
    std::string text;
    std::string debugFilePath;
    // Per-variant pipeline state. Transparent variants enable alpha blending and disable depth
    // writes; opaque variants (the default) keep blending off and depth writes on.
    bool blendEnable = false;
    bool depthWrite = true;
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

    // Additional fragment shaders that produce extra pipeline variants sharing this exact
    // pipeline layout. Combined with fragmentShaderText (variant 0), these are the pipelines
    // selectable per-draw through a device-generated-commands Indirect Execution Set.
    std::vector<ShaderVariant> fragmentShaderVariants;
    // Creates the pipelines with VK_PIPELINE_CREATE_2_INDIRECT_BINDABLE_BIT_EXT so they can be
    // bound from device-generated commands. Requires VK_EXT_device_generated_commands.
    bool indirectBindable = false;
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