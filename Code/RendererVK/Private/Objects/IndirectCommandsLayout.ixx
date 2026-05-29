export module RendererVK:IndirectCommandsLayout;

import Core;
import :VK;

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
