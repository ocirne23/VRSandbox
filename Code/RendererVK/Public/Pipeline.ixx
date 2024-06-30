export module RendererVK.Pipeline;

import Core;
import RendererVK.VK;

export class Device;
export class RenderPass;

export struct VertexLayoutInfo
{
	vk::VertexInputBindingDescription bindingDescription;
	std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
};

export struct PipelineLayout
{
	std::string fragmentShaderText;
	std::string vertexShaderText;
	uint32_t numUniformBuffers;
	uint32_t numSamplers;
	VertexLayoutInfo vertexLayoutInfo;
	std::vector<vk::DescriptorSetLayoutBinding> descriptorSetLayoutBindings;
};

export class Pipeline
{
public:
	Pipeline();
	~Pipeline();
	Pipeline(const Pipeline&) = delete;

	bool initialize(const Device& device, const RenderPass& renderPass, PipelineLayout& layout);

	vk::Pipeline getPipeline() const { return m_pipeline; }
	vk::PipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
	vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
	vk::DescriptorSet getDescriptorSet(uint32 currentFrameIndex) const { return m_descriptorSets[currentFrameIndex]; }
private:

	vk::Pipeline m_pipeline;
	vk::PipelineCache m_pipelineCache;
	vk::PipelineLayout m_pipelineLayout;
	vk::DescriptorSetLayout m_descriptorSetLayout;
	std::vector<vk::DescriptorSet> m_descriptorSets;
	vk::Device m_device;
};