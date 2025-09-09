export module RendererVK.CommandBuffer;

import Core;
import RendererVK.VK;

export struct DescriptorSetUpdateInfo
{
    uint32 binding;
    uint32 startIdx = 0;
    uint32 count = 1;
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

    bool initialize(vk::CommandBufferLevel level);

    vk::CommandBuffer begin(bool once = false, vk::CommandBufferInheritanceInfo* pInheritanceInfo = nullptr);
    void end();
    bool hasRecorded() { return m_hasRecorded; }
    void reset();
    vk::CommandBuffer getCommandBuffer() { return m_commandBuffer; }

    void submitGraphics(vk::Fence fence = VK_NULL_HANDLE);
    void addWaitSemaphore(vk::Semaphore semaphore, vk::PipelineStageFlags waitStageFlags) { m_waitSemaphores.push_back(semaphore); m_waitStages.push_back(waitStageFlags); }
    void addSignalSemaphore(vk::Semaphore semaphore) { m_signalSemaphores.push_back(semaphore); }
    const std::vector<vk::Semaphore>& getWaitSemaphores() const { return m_waitSemaphores; }
    const std::vector<vk::Semaphore>& getSignalSemaphores() const { return m_signalSemaphores; }
    void setWaitStage(vk::PipelineStageFlags2 stage) { m_waitStage = stage; }
    void cmdUpdateDescriptorSets(vk::PipelineLayout pipelineLayout, vk::PipelineBindPoint bindPoint, const std::span<DescriptorSetUpdateInfo>& updateInfo);

private:

    vk::CommandBuffer m_commandBuffer;
    bool m_hasRecorded = false;
    std::vector<vk::Semaphore> m_signalSemaphores;
    std::vector<vk::Semaphore> m_waitSemaphores;
    std::vector<vk::PipelineStageFlags> m_waitStages;
    vk::PipelineStageFlags2 m_waitStage = vk::PipelineStageFlagBits2::eNone;
};