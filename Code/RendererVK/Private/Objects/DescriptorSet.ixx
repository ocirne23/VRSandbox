export module RendererVK.DescriptorSet;

import Core;
import RendererVK.VK;

export class DescriptorSet final
{
public:

    DescriptorSet();
    ~DescriptorSet();
    DescriptorSet(const DescriptorSet&) = delete;

    bool initialize(vk::DescriptorSetLayout descriptorSetLayout);

    vk::DescriptorSet getDescriptorSet() const { return m_descriptorSet; }

private:

    vk::DescriptorSet m_descriptorSet;
};