export module RendererVK.CommandBuffer;

import Core;
import RendererVK.VK;

export struct DescriptorSetUpdateInfo
{
	uint32 binding;
	vk::DescriptorType type;
	std::variant<vk::DescriptorBufferInfo, vk::DescriptorImageInfo> info;
};

export class CommandBuffer final
{
public:
	CommandBuffer();
	~CommandBuffer();
	CommandBuffer(const CommandBuffer&) = delete;
	CommandBuffer(CommandBuffer&&) = default;

	bool initialize();

	void submitGraphics(vk::Fence fence = VK_NULL_HANDLE);
	vk::CommandBuffer getCommandBuffer() const { return m_commandBuffer; }
	void addWaitSemaphore(vk::Semaphore semaphore, vk::PipelineStageFlags waitStageFlags) { m_waitSemaphores.push_back(semaphore); m_waitStages.push_back(waitStageFlags); }
	void addSignalSemaphore(vk::Semaphore semaphore) { m_signalSemaphores.push_back(semaphore); }
	const std::vector<vk::Semaphore>& getWaitSemaphores() const { return m_waitSemaphores; }
	const std::vector<vk::Semaphore>& getSignalSemaphores() const { return m_signalSemaphores; }
	void setWaitStage(vk::PipelineStageFlags2 stage) { m_waitStage = stage; }
	void cmdUpdateDescriptorSets(vk::PipelineLayout pipelineLayout, const std::span<DescriptorSetUpdateInfo>& updateInfo);

private:

	vk::CommandBuffer m_commandBuffer;
	std::vector<vk::Semaphore> m_signalSemaphores;
	std::vector<vk::Semaphore> m_waitSemaphores;
	std::vector<vk::PipelineStageFlags> m_waitStages;
	vk::PipelineStageFlags2 m_waitStage = vk::PipelineStageFlagBits2::eNone;
};