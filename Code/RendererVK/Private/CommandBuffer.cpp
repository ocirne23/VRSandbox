module RendererVK.CommandBuffer;

import RendererVK.VK;
import RendererVK.Device;

CommandBuffer::CommandBuffer() {}
CommandBuffer::~CommandBuffer() 
{
	if (m_commandBuffer)
		m_device->getDevice().freeCommandBuffers(m_device->getCommandPool(), m_commandBuffer);
}

bool CommandBuffer::initialize(const Device& device)
{
	m_device = &device;
	vk::CommandBufferAllocateInfo allocInfo 
	{
		.commandPool = device.getCommandPool(),
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1,
	};
	m_commandBuffer = device.getDevice().allocateCommandBuffers(allocInfo)[0];
	if (!m_commandBuffer)
	{
		assert(false && "Failed to allocate command buffer");
		return false;
	}
	m_signalSemaphores.reserve(4);
	m_waitSemaphores.shrink_to_fit();

	m_waitSemaphores.reserve(4);
	m_waitSemaphores.shrink_to_fit();

	m_waitStages.reserve(4);
	m_waitStages.shrink_to_fit();
	return true;
}

void CommandBuffer::submitGraphics(vk::Fence fence)
{
	vk::SubmitInfo submitInfo {
		.waitSemaphoreCount = (uint32_t)m_waitSemaphores.size(),
		.pWaitSemaphores = m_waitSemaphores.data(),
		.pWaitDstStageMask = m_waitStages.data(),
		.commandBufferCount = 1,
		.pCommandBuffers = &m_commandBuffer,
		.signalSemaphoreCount = (uint32_t)m_signalSemaphores.size(),
		.pSignalSemaphores = m_signalSemaphores.data(),
	};
	m_device->getGraphicsQueue().submit(submitInfo, fence);

	m_waitSemaphores.clear();
	m_waitStages.clear();
	m_signalSemaphores.clear();
	m_waitStage = vk::PipelineStageFlagBits2::eNone;
}

void CommandBuffer::cmdUpdateDescriptorSets(vk::PipelineLayout pipelineLayout, const std::span<DescriptorSetUpdateInfo>& updateInfo)
{
	vk::WriteDescriptorSet* descriptorWrites = (vk::WriteDescriptorSet*)_alloca(sizeof(vk::WriteDescriptorSet) * updateInfo.size());
	for (uint32_t i = 0; i < updateInfo.size(); i++)
	{
		descriptorWrites[i] =
		{
				.dstBinding = updateInfo[i].binding,
				.dstArrayElement = 0,
				.descriptorCount = 1,
				.descriptorType = updateInfo[i].type,
				.pImageInfo = std::get_if<vk::DescriptorImageInfo>(&updateInfo[i].info),
				.pBufferInfo = std::get_if<vk::DescriptorBufferInfo>(&updateInfo[i].info),
		};
	}
	m_commandBuffer.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, (uint32_t)updateInfo.size(), descriptorWrites);
}