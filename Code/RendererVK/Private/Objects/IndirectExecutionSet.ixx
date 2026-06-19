export module RendererVK.IndirectExecutionSet;

import Core;
import RendererVK.fwd;
import RendererVK.VK;

export class IndirectExecutionSet final
{
public:
    IndirectExecutionSet();
    ~IndirectExecutionSet();
    IndirectExecutionSet(const IndirectExecutionSet&) = delete;

    bool initialize(const GraphicsPipeline& pipeline);
    void destroy();

    vk::IndirectExecutionSetEXT getHandle() const { return m_indirectExecutionSet; }

private:

    vk::IndirectExecutionSetEXT m_indirectExecutionSet;
};
