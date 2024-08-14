module RendererVK.CommandBuffer;

import RendererVK.VK;
import RendererVK.Device;

CommandBuffer::CommandBuffer() {}
CommandBuffer::~CommandBuffer()
{
    if (m_commandBuffer)
        Globals::device.getDevice().freeCommandBuffers(Globals::device.getCommandPool(), m_commandBuffer);
}

bool CommandBuffer::initialize()
{
    vk::CommandBufferAllocateInfo allocInfo
    {
        .commandPool = Globals::device.getCommandPool(),
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1,
    };
    m_commandBuffer = Globals::device.getDevice().allocateCommandBuffers(allocInfo)[0];
    if (!m_commandBuffer)
    {
        assert(false && "Failed to allocate command buffer");
        return false;
    }
    m_signalSemaphores.reserve(4);
    m_signalSemaphores.shrink_to_fit();

    m_waitSemaphores.reserve(4);
    m_waitSemaphores.shrink_to_fit();

    m_waitStages.reserve(4);
    m_waitStages.shrink_to_fit();
    return true;
}

void CommandBuffer::submitGraphics(vk::Fence fence)
{
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = (uint32)m_waitSemaphores.size(),
        .pWaitSemaphores = m_waitSemaphores.data(),
        .pWaitDstStageMask = m_waitStages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &m_commandBuffer,
        .signalSemaphoreCount = (uint32)m_signalSemaphores.size(),
        .pSignalSemaphores = m_signalSemaphores.data(),
    };
    Globals::device.getGraphicsQueue().submit(submitInfo, fence);

    m_waitSemaphores.clear();
    m_waitStages.clear();
    m_signalSemaphores.clear();
    m_waitStage = vk::PipelineStageFlagBits2::eNone;
}

void CommandBuffer::cmdUpdateDescriptorSets(vk::PipelineLayout pipelineLayout, vk::PipelineBindPoint bindPoint, const std::span<DescriptorSetUpdateInfo>& updateInfo)
{
    vk::WriteDescriptorSet* descriptorWrites = (vk::WriteDescriptorSet*)_alloca(sizeof(vk::WriteDescriptorSet) * updateInfo.size());
    for (uint32 i = 0; i < updateInfo.size(); i++)
    {
        descriptorWrites[i] =
        {
            .dstBinding = updateInfo[i].binding,
            .dstArrayElement = updateInfo[i].startIdx,
            .descriptorCount = updateInfo[i].count,
            .descriptorType = updateInfo[i].type,
            .pImageInfo = std::get_if<vk::DescriptorImageInfo>(&updateInfo[i].info),
            .pBufferInfo = std::get_if<vk::DescriptorBufferInfo>(&updateInfo[i].info),
        };
    }
    m_commandBuffer.pushDescriptorSetKHR(bindPoint, pipelineLayout, 0, (uint32)updateInfo.size(), descriptorWrites);
}

vk::CommandBuffer CommandBuffer::begin(bool once)
{
    if (m_hasRecorded)
        m_commandBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
    m_commandBuffer.begin({ .flags = once ? vk::CommandBufferUsageFlagBits::eOneTimeSubmit : (vk::CommandBufferUsageFlagBits)0 });
    m_hasRecorded = true;

    return m_commandBuffer;
}

void CommandBuffer::end()
{
    m_commandBuffer.end();
}

void CommandBuffer::reset()
{
    m_hasRecorded = false;
    m_commandBuffer.reset(vk::CommandBufferResetFlagBits::eReleaseResources);
}