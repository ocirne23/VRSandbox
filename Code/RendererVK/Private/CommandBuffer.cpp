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
	/*
	vk::CommandBufferSubmitInfo cmdSubmitInfo {
		.commandBuffer = m_commandBuffer,
	};
	vk::SemaphoreSubmitInfo* pWaitSemaphoreInfos = (vk::SemaphoreSubmitInfo*)alloca(sizeof(vk::SemaphoreSubmitInfo) * m_waitSemaphores.size());
	for (size_t i = 0; i < m_waitSemaphores.size(); i++)
	{
		pWaitSemaphoreInfos[i] = vk::SemaphoreSubmitInfo {
			.semaphore = m_waitSemaphores[i],
			.stageMask = m_waitStage,
		};
	};
	vk::SemaphoreSubmitInfo* pSignalSemaphoreInfos = (vk::SemaphoreSubmitInfo*)alloca(sizeof(vk::SemaphoreSubmitInfo) * m_signalSemaphores.size());
	for (size_t i = 0; i < m_signalSemaphores.size(); i++)
	{
		pSignalSemaphoreInfos[i] = vk::SemaphoreSubmitInfo {
			.semaphore = m_signalSemaphores[i],
			.stageMask = m_waitStage,
		};
	};

	vk::SubmitInfo2 submitInfo2 {
		.waitSemaphoreInfoCount = (uint32_t)m_waitSemaphores.size(),
		.pWaitSemaphoreInfos = pWaitSemaphoreInfos,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdSubmitInfo,
		.signalSemaphoreInfoCount = (uint32_t)m_signalSemaphores.size(),
		.pSignalSemaphoreInfos = pSignalSemaphoreInfos,
	};
	vk::Result result = m_device->getGraphicsQueue().submit2(1, &submitInfo2, fence);
	assert(result == vk::Result::eSuccess);
	*/
	m_waitSemaphores.clear();
	m_waitStages.clear();
	m_signalSemaphores.clear();
	m_waitStage = vk::PipelineStageFlagBits2::eNone;
}