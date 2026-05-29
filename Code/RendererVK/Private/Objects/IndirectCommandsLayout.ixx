export module RendererVK:IndirectCommandsLayout;

import Core;
import :VK;

// Wraps a VK_EXT_device_generated_commands VkIndirectCommandsLayoutEXT describing one draw
// sequence: an EXECUTION_SET token (selects a pipeline variant from the Indirect Execution Set)
// followed by a DRAW_INDEXED token. The per-sequence buffer layout matches
// RendererVKLayout::IndirectDrawSequence.
export class IndirectCommandsLayout final
{
public:
    IndirectCommandsLayout();
    ~IndirectCommandsLayout();
    IndirectCommandsLayout(const IndirectCommandsLayout&) = delete;

    // shaderStages must cover every stage swapped by the execution set / touched by the draw.
    bool initialize(vk::PipelineLayout pipelineLayout, vk::ShaderStageFlags shaderStages);
    void destroy();

    vk::IndirectCommandsLayoutEXT getHandle() const { return m_layout; }
    uint32 getIndirectStride() const { return m_indirectStride; }

private:

    vk::IndirectCommandsLayoutEXT m_layout;
    uint32 m_indirectStride = 0;
};
