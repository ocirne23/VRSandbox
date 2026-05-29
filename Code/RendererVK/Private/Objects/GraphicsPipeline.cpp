module RendererVK:GraphicsPipeline;

import Core;
import :VK;
import :Device;
import :Shader;
import :RenderPass;

GraphicsPipeline::GraphicsPipeline() {}
GraphicsPipeline::~GraphicsPipeline()
{
    vk::Device vkDevice = Globals::device.getDevice();
    for (vk::Pipeline pipeline : m_pipelines)
        vkDevice.destroyPipeline(pipeline);
    if (m_pipelineCache)
        vkDevice.destroyPipelineCache(m_pipelineCache);
    if (m_pipelineLayout)
        vkDevice.destroyPipelineLayout(m_pipelineLayout);
    if (m_descriptorSetLayout)
        vkDevice.destroyDescriptorSetLayout(m_descriptorSetLayout);
}

bool GraphicsPipeline::initialize(const RenderPass& renderPass, GraphicsPipelineLayout& layout)
{
    vk::Device vkDevice = Globals::device.getDevice();
    vk::DescriptorSetLayoutCreateInfo layoutInfo
    {
        .bindingCount = (uint32)layout.descriptorSetLayoutBindings.size(),
        .pBindings = layout.descriptorSetLayoutBindings.data(),
    };
    auto createLayoutResult = vkDevice.createDescriptorSetLayout(layoutInfo);
    if (createLayoutResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create descriptor set layout");
        return false;
    }
	m_descriptorSetLayout = createLayoutResult.value;

    vk::PipelineLayoutCreateInfo pipelineLayoutCreateInfo
    {
        .flags = {},
        .setLayoutCount = 1,
        .pSetLayouts = &m_descriptorSetLayout,
        .pushConstantRangeCount = (uint32)layout.pushConstantRanges.size(),
        .pPushConstantRanges = layout.pushConstantRanges.data(),
    };
    auto createPipelineLayoutResult = vkDevice.createPipelineLayout(pipelineLayoutCreateInfo);
    if (createPipelineLayoutResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create pipeline layout");
        return false;
    }
    m_pipelineLayout = createPipelineLayoutResult.value;

    // Variant 0 is fragmentShaderText; any fragmentShaderVariants follow. All variants share this
    // pipeline layout and differ only in the fragment stage, so a device-generated-commands
    // Indirect Execution Set can select between them per draw.
    std::vector<const ShaderVariant*> fragmentVariants;
    ShaderVariant defaultVariant{ .text = layout.fragmentShaderText, .debugFilePath = layout.fragmentShaderDebugFilePath };
    fragmentVariants.push_back(&defaultVariant);
    for (const ShaderVariant& variant : layout.fragmentShaderVariants)
        fragmentVariants.push_back(&variant);

    Shader vertexShader;
    vertexShader.initialize(vk::ShaderStageFlagBits::eVertex, layout.vertexShaderText, layout.vertexShaderDebugFilePath);

    std::vector<Shader> fragmentShaders(fragmentVariants.size());
    for (size_t i = 0; i < fragmentVariants.size(); i++)
        fragmentShaders[i].initialize(vk::ShaderStageFlagBits::eFragment, fragmentVariants[i]->text, fragmentVariants[i]->debugFilePath);

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
            .module = nullptr, // set per-variant below
            .pName = "main",
        },
    };

    vk::PipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo
    {
        .flags = {},
        .vertexBindingDescriptionCount = (uint32)layout.vertexLayoutInfo.bindingDescriptions.size(),
        .pVertexBindingDescriptions = layout.vertexLayoutInfo.bindingDescriptions.data(),
        .vertexAttributeDescriptionCount = (uint32)layout.vertexLayoutInfo.attributeDescriptions.size(),
        .pVertexAttributeDescriptions = layout.vertexLayoutInfo.attributeDescriptions.data(),
    };
    vk::PipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo
    {
        .flags = {},
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
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
        .depthClampEnable = vk::False,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = vk::False,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f,
    };
    vk::PipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo
    {
        .flags = {},
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
        .minSampleShading = 0.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = vk::False,
        .alphaToOneEnable = vk::False,
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
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
        .depthCompareOp = vk::CompareOp::eLess,
        .depthBoundsTestEnable = vk::False,
        .stencilTestEnable = vk::False,
        //	.front = stencilOpState,
        //	.back = stencilOpState,
        //	.minDepthBounds = 0.0f,
        //	.maxDepthBounds = 0.0f,
    };

    vk::ColorComponentFlags colorComponentFlags = vk::FlagTraits<vk::ColorComponentFlagBits>::allFlags;
    vk::PipelineColorBlendAttachmentState pipelineColorBlendAttachmentState
    {
        .blendEnable = vk::False,
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
        .logicOpEnable = vk::False,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &pipelineColorBlendAttachmentState,
        .blendConstants = { { 1.0f, 1.0f, 1.0f, 1.0f } },
    };
    std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    vk::PipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo
    {
        .flags = {},
        .dynamicStateCount = static_cast<uint32>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    // When the pipelines must be bindable from device-generated commands, the bindable flag is
    // supplied through PipelineCreateFlags2CreateInfo (maintenance5), which overrides .flags.
    vk::PipelineCreateFlags2CreateInfo pipelineFlags2{ .flags = vk::PipelineCreateFlagBits2::eIndirectBindableEXT };
    vk::GraphicsPipelineCreateInfo graphicsPipelineCreateInfo
    {
        .pNext = layout.indirectBindable ? &pipelineFlags2 : nullptr,
        .flags = {},
        .stageCount = static_cast<uint32>(pipelineShaderStageCreateInfos.size()),
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

    auto createPipelineCacheResult = vkDevice.createPipelineCache(vk::PipelineCacheCreateInfo());
    if (createPipelineCacheResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create pipeline cache");
        return false;
    }
    m_pipelineCache = createPipelineCacheResult.value;

    m_pipelines.reserve(fragmentShaders.size());
    for (size_t i = 0; i < fragmentShaders.size(); i++)
    {
        const ShaderVariant& variant = *fragmentVariants[i];
        pipelineShaderStageCreateInfos[1].module = fragmentShaders[i].getModule();

        // Per-variant blend/depth state (mutated in place; the create info points at these structs).
        pipelineDepthStencilStateCreateInfo.depthWriteEnable = variant.depthWrite ? vk::True : vk::False;
        pipelineColorBlendAttachmentState.blendEnable = variant.blendEnable ? vk::True : vk::False;
        if (variant.blendEnable)
        {
            // Standard "over" alpha blending: src.rgb*src.a + dst.rgb*(1-src.a), keep dst alpha.
            pipelineColorBlendAttachmentState.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            pipelineColorBlendAttachmentState.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            pipelineColorBlendAttachmentState.colorBlendOp = vk::BlendOp::eAdd;
            pipelineColorBlendAttachmentState.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            pipelineColorBlendAttachmentState.dstAlphaBlendFactor = vk::BlendFactor::eZero;
            pipelineColorBlendAttachmentState.alphaBlendOp = vk::BlendOp::eAdd;
        }

        vk::Result   result;
        vk::Pipeline pipeline;
        std::tie(result, pipeline) = vkDevice.createGraphicsPipeline(m_pipelineCache, graphicsPipelineCreateInfo);
        switch (result)
        {
        case vk::Result::eSuccess:
            break;
        default:
            assert(false && "Failed to create graphics pipeline");
            return false;
        }
        m_pipelines.push_back(pipeline);
    }
    return true;
}