module RendererVK:IndirectCommandsLayout;

import Core;
import :Device;
import :Layout;

IndirectCommandsLayout::IndirectCommandsLayout() {}
IndirectCommandsLayout::~IndirectCommandsLayout()
{
    destroy();
}

bool IndirectCommandsLayout::initialize(vk::PipelineLayout pipelineLayout, vk::ShaderStageFlags shaderStages)
{
    // Token 0: select the pipeline variant for this sequence from the Indirect Execution Set.
    vk::IndirectCommandsExecutionSetTokenEXT executionSetToken{
        .type = vk::IndirectExecutionSetInfoTypeEXT::ePipelines,
        .shaderStages = shaderStages,
    };
    vk::IndirectCommandsTokenDataEXT executionSetTokenData;
    executionSetTokenData.pExecutionSet = &executionSetToken;

    std::array<vk::IndirectCommandsLayoutTokenEXT, 2> tokens{
        vk::IndirectCommandsLayoutTokenEXT{
            .type = vk::IndirectCommandsTokenTypeEXT::eExecutionSet,
            .data = executionSetTokenData,
            .offset = offsetof(RendererVKLayout::IndirectDrawSequence, pipelineIndex),
        },
        // DRAW_INDEXED carries no token data struct; its buffer payload is a VkDrawIndexedIndirectCommand.
        vk::IndirectCommandsLayoutTokenEXT{
            .type = vk::IndirectCommandsTokenTypeEXT::eDrawIndexed,
            .offset = offsetof(RendererVKLayout::IndirectDrawSequence, indexCount),
        },
    };

    m_indirectStride = (uint32)sizeof(RendererVKLayout::IndirectDrawSequence);

    vk::IndirectCommandsLayoutCreateInfoEXT createInfo{
        .shaderStages = shaderStages,
        .indirectStride = m_indirectStride,
        .pipelineLayout = pipelineLayout,
        .tokenCount = (uint32)tokens.size(),
        .pTokens = tokens.data(),
    };
    auto createResult = Globals::device.getDevice().createIndirectCommandsLayoutEXT(createInfo);
    if (createResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create indirect commands layout");
        return false;
    }
    m_layout = createResult.value;
    return true;
}

void IndirectCommandsLayout::destroy()
{
    if (m_layout)
    {
        Globals::device.getDevice().destroyIndirectCommandsLayoutEXT(m_layout);
        m_layout = nullptr;
    }
}
