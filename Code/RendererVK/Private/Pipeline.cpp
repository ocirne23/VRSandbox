module;

#include "VK.h"
#include <variant>

module RendererVK.Pipeline;

import RendererVK.Device;
import RendererVK.Shader;
import RendererVK.RenderPass;

Pipeline::Pipeline() {}
Pipeline::~Pipeline()
{
	if (m_pipeline)
		m_device.destroyPipeline(m_pipeline);
	if (m_pipelineCache)
		m_device.destroyPipelineCache(m_pipelineCache);
	if (m_pipelineLayout)
		m_device.destroyPipelineLayout(m_pipelineLayout);
	if (m_descriptorSetLayout)
		m_device.destroyDescriptorSetLayout(m_descriptorSetLayout);
}

bool Pipeline::initialize(const Device& device, const RenderPass& renderPass, PipelineLayout& layout)
{
	m_device = device.getDevice();
	vk::DescriptorSetLayoutCreateInfo layoutInfo
	{
		.bindingCount = (uint32_t)layout.descriptorSetLayoutBindings.size(),
		.pBindings = layout.descriptorSetLayoutBindings.data(),
	};
	m_descriptorSetLayout = m_device.createDescriptorSetLayout(layoutInfo);
	if (!m_descriptorSetLayout)
	{
		assert(false && "Failed to create descriptor set layout");
		return false;
	}

	vk::DescriptorSetAllocateInfo descriptorSetAllocateInfo
	{
		.descriptorPool = device.getDescriptorPool(),
		.descriptorSetCount = 1,
		.pSetLayouts = &m_descriptorSetLayout,
	};
	m_descriptorSet = m_device.allocateDescriptorSets(descriptorSetAllocateInfo)[0];
	if (!m_descriptorSet)
	{
		assert(false && "Failed to allocate descriptor set");
		return false;
	}

	vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo
	{
		.flags = {},
		.setLayoutCount = 1,
		.pSetLayouts = &m_descriptorSetLayout,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = nullptr,
	};
	m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutCreateInfo);
	if (!m_pipelineLayout)
	{
		assert(false && "Failed to create pipeline layout");
		return false;
	}

	Shader vertexShader, fragmentShader;
	vertexShader.initialize(device, vk::ShaderStageFlagBits::eVertex, layout.vertexShaderText);
	fragmentShader.initialize(device, vk::ShaderStageFlagBits::eFragment, layout.fragmentShaderText);
	std::array<vk::PipelineShaderStageCreateInfo, 2> pipelineShaderStageCreateInfos =
	{
		vk::PipelineShaderStageCreateInfo
		{
			.flags = {},
			.stage = vk::ShaderStageFlagBits::eVertex,
			.module = vertexShader.getModule(),
			.pName = "main",
		},
		vk::PipelineShaderStageCreateInfo
		{
			.flags = {},
			.stage = vk::ShaderStageFlagBits::eFragment,
			.module = fragmentShader.getModule(),
			.pName = "main",
		},
	};

	vk::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo
	{
		.flags = {},
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &layout.vertexLayoutInfo.bindingDescription,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(layout.vertexLayoutInfo.attributeDescriptions.size()),
		.pVertexAttributeDescriptions = layout.vertexLayoutInfo.attributeDescriptions.data(),
	};

	vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo
	{
		.flags = {},
		.topology = vk::PrimitiveTopology::eTriangleList,
		.primitiveRestartEnable = VK_FALSE,
	};
	vk::PipelineViewportStateCreateInfo pipelineViewportStateCreateInfo
	{
		.flags = {},
		.viewportCount = 1,
		.pViewports = nullptr,
		.scissorCount = 1,
		.pScissors = nullptr,
	};
	vk::PipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo
	{
		.flags = {},
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = vk::PolygonMode::eFill,
		.cullMode = vk::CullModeFlagBits::eBack,
		.frontFace = vk::FrontFace::eClockwise,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = 0.0f,
		.lineWidth = 1.0f,
	};

	vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo
	{
		.flags = {},
		.rasterizationSamples = vk::SampleCountFlagBits::e1,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 0.0f,
		.pSampleMask = nullptr,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};

	vk::StencilOpState stencilOpState
	{
		.failOp = vk::StencilOp::eKeep,
		.passOp = vk::StencilOp::eKeep,
		.depthFailOp = vk::StencilOp::eKeep,
		.compareOp = vk::CompareOp::eAlways,
		.compareMask = 0,
		.writeMask = 0,
		.reference = 0,
	};
	vk::PipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo{
		.flags = {},
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = vk::CompareOp::eLess,
		.depthBoundsTestEnable = VK_FALSE,
		.stencilTestEnable = VK_FALSE,
	//	.front = stencilOpState,
	//	.back = stencilOpState,
	//	.minDepthBounds = 0.0f,
	//	.maxDepthBounds = 0.0f,
	};

	vk::ColorComponentFlags colorComponentFlags = vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags;
	vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState
	{
		.blendEnable = VK_FALSE,
		.srcColorBlendFactor = vk::BlendFactor::eZero,
		.dstColorBlendFactor = vk::BlendFactor::eZero,
		.colorBlendOp = vk::BlendOp::eAdd,
		.srcAlphaBlendFactor = vk::BlendFactor::eZero,
		.dstAlphaBlendFactor = vk::BlendFactor::eZero,
		.alphaBlendOp = vk::BlendOp::eAdd,
		.colorWriteMask = colorComponentFlags,
	};
	vk::PipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo
	{
		.flags = {},
		.logicOpEnable = VK_FALSE,
		.logicOp = vk::LogicOp::eCopy,
		.attachmentCount = 1,
		.pAttachments = &pipelineColorBlendAttachmentState,
		.blendConstants = { { 1.0f, 1.0f, 1.0f, 1.0f } },
	};
	std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo
	{
		.flags = {},
		.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
		.pDynamicStates = dynamicStates.data(),
	};
	vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo
	{
		.flags = {},
		.stageCount = static_cast<uint32_t>(pipelineShaderStageCreateInfos.size()),
		.pStages = pipelineShaderStageCreateInfos.data(),
		.pVertexInputState = &pipelineVertexInputStateCreateInfo,
		.pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo,
		.pTessellationState = nullptr,
		.pViewportState = &pipelineViewportStateCreateInfo,
		.pRasterizationState = &pipelineRasterizationStateCreateInfo,
		.pMultisampleState = &pipelineMultisampleStateCreateInfo,
		.pDepthStencilState = &pipelineDepthStencilStateCreateInfo,
		.pColorBlendState = &pipelineColorBlendStateCreateInfo,
		.pDynamicState = &pipelineDynamicStateCreateInfo,
		.layout = m_pipelineLayout,
		.renderPass = renderPass.getRenderPass(),
		.subpass = 0,
		.basePipelineHandle = nullptr,
		.basePipelineIndex = 0,
	};

	m_pipelineCache = m_device.createPipelineCache(vk::PipelineCacheCreateInfo());

	vk::Result   result;
	vk::Pipeline pipeline;
	std::tie(result, pipeline) = m_device.createGraphicsPipeline(m_pipelineCache, graphicsPipelineCreateInfo);
	switch (result)
	{
	case vk::Result::eSuccess:
		break;
	default:
		assert(false && "Failed to create graphics pipeline");
		return false;
	}

	m_pipeline = pipeline;

	return true;
}

void Pipeline::updateDescriptorSets(const std::span<DescriptorSetUpdateInfo>& updateInfo)
{
	vk::WriteDescriptorSet* descriptorWrites = (vk::WriteDescriptorSet*)alloca(sizeof(vk::WriteDescriptorSet) * updateInfo.size());
	for (uint32_t i = 0; i < updateInfo.size(); i++)
	{
		descriptorWrites[i] =
		{
			.dstSet = m_descriptorSet,
			.dstBinding = updateInfo[i].binding,
			.dstArrayElement = 0,
			.descriptorCount = 1,
			.descriptorType = updateInfo[i].type,
			.pImageInfo = std::get_if<vk::DescriptorImageInfo>(&updateInfo[i].info),
			.pBufferInfo = std::get_if<vk::DescriptorBufferInfo>(&updateInfo[i].info),
		};
	}
	m_device.updateDescriptorSets((uint32_t)updateInfo.size(), descriptorWrites, 0, nullptr);
}

/*

	vk::WriteDescriptorSet descriptorWrite[2];
	descriptorWrite[0] =
	{
		.dstSet = m_descriptorSet,
		.dstBinding = 0,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.pBufferInfo = &bufferInfo,
	};
	descriptorWrite[1] =
	{
		.dstSet = m_descriptorSet,
		.dstBinding = 1,
		.dstArrayElement = 0,
		.descriptorCount = 1,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.pImageInfo = &imageInfo,
	};
	device.getDevice().updateDescriptorSets(1, descriptorWrite, 0, nullptr);
*/