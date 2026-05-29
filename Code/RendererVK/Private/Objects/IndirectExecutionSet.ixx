export module RendererVK:IndirectExecutionSet;

import Core;
import :VK;

export class GraphicsPipeline;

// Wraps a VK_EXT_device_generated_commands Indirect Execution Set of type "pipelines".
// Holds every pipeline variant of a GraphicsPipeline so device-generated commands can select
// one per draw via an EXECUTION_SET token. All source pipelines must share one pipeline layout.
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
