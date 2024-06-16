module;

#include "VK.h"
#include <variant>

export module RendererVK.Pipeline;

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

export struct DescriptorSetUpdateInfo
{
	uint32_t binding;
	vk::DescriptorType type;
	std::variant<vk::DescriptorBufferInfo, vk::DescriptorImageInfo> info;
};

export class Pipeline
{
public:
	Pipeline();
	~Pipeline();
	Pipeline(const Pipeline&) = delete;

	bool initialize(const Device& device, const RenderPass& renderPass, PipelineLayout& layout);
	void updateDescriptorSets(const std::span<DescriptorSetUpdateInfo>& updateInfo);

	vk::Pipeline getPipeline() const { return m_pipeline; }
	vk::PipelineLayout getPipelineLayout() const { return m_pipelineLayout; }
	vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_descriptorSetLayout; }
	vk::DescriptorSet getDescriptorSet() const { return m_descriptorSet; }
private:

	vk::Pipeline m_pipeline;
	vk::PipelineCache m_pipelineCache;
	vk::PipelineLayout m_pipelineLayout;
	vk::DescriptorSetLayout m_descriptorSetLayout;
	vk::DescriptorSet m_descriptorSet;
	vk::Device m_device;
};