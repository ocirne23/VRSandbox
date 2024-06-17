export module RendererVK.CommandBuffer;

import RendererVK.VK;

export class Device;

export class CommandBuffer
{
public:
	CommandBuffer();
	~CommandBuffer();
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) = default;

	bool initialize(const Device& device);

	void submitGraphics(vk::Fence fence = VK_NULL_HANDLE);

	vk::CommandBuffer getCommandBuffer() const { return m_commandBuffer; }

	void addWaitSemaphore(vk::Semaphore semaphore, vk::PipelineStageFlags waitStageFlags) { m_waitSemaphores.push_back(semaphore); m_waitStages.push_back(waitStageFlags); }
	void addSignalSemaphore(vk::Semaphore semaphore) { m_signalSemaphores.push_back(semaphore); }

	const std::vector<vk::Semaphore>& getWaitSemaphores() const { return m_waitSemaphores; }
	const std::vector<vk::Semaphore>& getSignalSemaphores() const { return m_signalSemaphores; }

	void setWaitStage(vk::PipelineStageFlags2 stage) { m_waitStage = stage; }

private:

	const Device* m_device = nullptr;
	vk::CommandBuffer m_commandBuffer;
	std::vector<vk::Semaphore> m_signalSemaphores;
	std::vector<vk::Semaphore> m_waitSemaphores;
	std::vector<vk::PipelineStageFlags> m_waitStages;
	vk::PipelineStageFlags2 m_waitStage = vk::PipelineStageFlagBits2::eNone;
};