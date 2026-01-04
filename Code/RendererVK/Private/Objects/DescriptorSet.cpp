module RendererVK:DescriptorSet;

import :Device;

DescriptorSet::DescriptorSet() {}
DescriptorSet::~DescriptorSet() 
{
    if (m_descriptorSet)
    {
        vk::Device vkDevice = Globals::device.getDevice();
        vkDevice.freeDescriptorSets(Globals::device.getDescriptorPool(), m_descriptorSet);
        m_descriptorSet = nullptr;
    }
}
bool DescriptorSet::initialize(vk::DescriptorSetLayout descriptorSetLayout)
{
    vk::Device vkDevice = Globals::device.getDevice();
    vk::DescriptorSetAllocateInfo allocInfo
    {
        .descriptorPool = Globals::device.getDescriptorPool(),
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
    };
    auto allocateResult = vkDevice.allocateDescriptorSets(allocInfo);
    if (allocateResult.result != vk::Result::eSuccess || !allocateResult.value[0])
    {
        assert(false && "Failed to allocate descriptor set");
        return false;
    }
    m_descriptorSet = allocateResult.value[0];
    return true;
}