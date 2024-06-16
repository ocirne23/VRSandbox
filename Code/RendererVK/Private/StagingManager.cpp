module;

#include "VK.h"

module RendererVK.StagingManager;

import RendererVK.Device;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.SwapChain;

static constexpr size_t STAGING_BUFFER_SIZE = 1024 * 1024;

StagingManager::StagingManager()
{
}

StagingManager::~StagingManager()
{
}

bool StagingManager::initialize(Device& device)
{
	m_device = &device;
	vk::Device vkDevice = device.getDevice();
	for (int i = 0; i < NUM_STAGING_BUFFERS; i++)
	{
		if (!m_stagingBuffers[i].initialize(device, STAGING_BUFFER_SIZE, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
			return false;

		m_commandBuffers[i].initialize(device);

		vk::FenceCreateInfo fenceCreateInfo = { .flags = vk::FenceCreateFlagBits::eSignaled };
		m_fences[i] = vkDevice.createFence(fenceCreateInfo);

		vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
		m_semaphores[i] = vkDevice.createSemaphore(semaphoreCreateInfo);
	}

	return true;
}

vk::Semaphore StagingManager::upload(vk::Buffer dstBuffer, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset)
{
	assert(dataSize <= STAGING_BUFFER_SIZE);
	if (m_currentBufferOffset + dataSize > STAGING_BUFFER_SIZE)
	{
		printf("Staging buffer full!\n");
		__debugbreak();
		return m_semaphores[m_currentBuffer];
	}
	if (!m_mappedMemory)
		m_mappedMemory = m_stagingBuffers[m_currentBuffer].mapMemory().data();

	memcpy(m_mappedMemory + m_currentBufferOffset, data, dataSize);
	m_copyRegions.emplace_back(std::pair<vk::Buffer, vk::BufferCopy>{ dstBuffer, vk::BufferCopy{ .srcOffset = m_currentBufferOffset, .dstOffset = dstOffset, .size = dataSize } });
	m_currentBufferOffset += dataSize;

	return m_semaphores[m_currentBuffer];
}

void StagingManager::update(SwapChain& swapChain)
{
	if (m_copyRegions.empty())
		return;

	if (m_mappedMemory)
	{
		m_stagingBuffers[m_currentBuffer].unmapMemory();
		m_mappedMemory = nullptr;
	}

	vk::Device device = m_device->getDevice();
	vk::Result result = device.waitForFences(1, &m_fences[m_currentBuffer], VK_TRUE, UINT64_MAX);
	if (result != vk::Result::eSuccess)
		assert(false && "Failed to wait for fence");
	result = device.resetFences(1, &m_fences[m_currentBuffer]);
	if (result != vk::Result::eSuccess)
		assert(false && "Failed to reset fence");

	CommandBuffer& commandBuffer = m_commandBuffers[m_currentBuffer];
	commandBuffer.addSignalSemaphore(m_semaphores[m_currentBuffer]);
	vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
	vkCommandBuffer.reset({});
	vkCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	for (const auto& copyRegion : m_copyRegions)
		vkCommandBuffer.copyBuffer(m_stagingBuffers[m_currentBuffer].getBuffer(), copyRegion.first, 1, &copyRegion.second);
	vkCommandBuffer.end();
	commandBuffer.submitGraphics(m_fences[m_currentBuffer]);

	swapChain.getCurrentCommandBuffer().addWaitSemaphore(m_semaphores[m_currentBuffer], vk::PipelineStageFlagBits::eVertexInput);
	
	m_currentBuffer = (m_currentBuffer + 1) % NUM_STAGING_BUFFERS;
	m_currentBufferOffset = 0;
	m_copyRegions.clear();
}