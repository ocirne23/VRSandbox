export module RendererVK.StagingManager;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;

export class Device;
export class SwapChain;

export class StagingManager
{
public:

	StagingManager();
	~StagingManager();
	StagingManager(const StagingManager&) = delete;

	bool initialize(Device& device);

	vk::Semaphore upload(vk::Buffer dstBuffer, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset = 0);
	void update(SwapChain& swapChain);

private:

	Device* m_device = nullptr;
	static constexpr int NUM_STAGING_BUFFERS = 2;
	Buffer m_stagingBuffers[NUM_STAGING_BUFFERS];
	vk::Fence m_fences[NUM_STAGING_BUFFERS];
	vk::Semaphore m_semaphores[NUM_STAGING_BUFFERS];
	CommandBuffer m_commandBuffers[NUM_STAGING_BUFFERS];

	int m_currentBuffer = 0;
	vk::DeviceSize m_currentBufferOffset = 0;
	std::vector<std::pair<vk::Buffer, vk::BufferCopy>> m_copyRegions;
	uint8* m_mappedMemory = nullptr;
};