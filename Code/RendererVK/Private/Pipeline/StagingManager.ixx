export module RendererVK.StagingManager;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;

export class SwapChain;

export class StagingManager final
{
public:

    StagingManager();
    ~StagingManager();
    StagingManager(const StagingManager&) = delete;

    bool initialize(SwapChain& swapChain);

    vk::Semaphore upload(vk::Buffer dstBuffer, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset = 0);
    vk::Semaphore uploadImage(vk::Image dstImage, uint32 imageWidth, uint32 imageHeight, vk::DeviceSize dataSize, const void* data, uint32 mipLevel, vk::DeviceSize dstOffset = 0);
    void transitionImage(vk::ImageMemoryBarrier2 barrier) { m_imageTransitions.push_back(barrier); }
    void update();

private:

    SwapChain* m_swapChain = nullptr;

    static constexpr int NUM_STAGING_BUFFERS = 2;
    Buffer m_stagingBuffers[NUM_STAGING_BUFFERS];
    uint8* m_mappedStagingBuffers[NUM_STAGING_BUFFERS];
    vk::Fence m_fences[NUM_STAGING_BUFFERS];
    vk::Semaphore m_semaphores[NUM_STAGING_BUFFERS];
    CommandBuffer m_commandBuffers[NUM_STAGING_BUFFERS];

    int m_currentBuffer = 0;
    vk::DeviceSize m_currentBufferOffset = 0;
    std::vector<std::pair<vk::Buffer, vk::BufferCopy>> m_bufferCopyRegions;
    std::vector<std::pair<vk::Image, vk::BufferImageCopy>> m_imageCopyRegions;
    std::vector<vk::ImageMemoryBarrier2> m_imageTransitions;
    uint8* m_mappedMemory = nullptr;
};