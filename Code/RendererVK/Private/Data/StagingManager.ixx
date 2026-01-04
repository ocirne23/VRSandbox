export module RendererVK.StagingManager;
extern "C++" {

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

    bool initialize();

    vk::Semaphore upload(vk::Buffer dstBuffer, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset = 0);
    vk::Semaphore uploadImage(vk::Image dstImage, uint32 imageWidth, uint32 imageHeight, vk::DeviceSize dataSize, const void* data, uint32 mipLevel, vk::DeviceSize dstOffset = 0);
    vk::Semaphore uploadImageAndGenerateMipMaps(vk::Image image, uint32 imageWidth, uint32 imageHeight, uint32 numMipLevels, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset = 0);
    vk::Semaphore update();

private:

    SwapChain* m_swapChain = nullptr;

    static constexpr int NUM_STAGING_BUFFERS = 2;
    Buffer m_stagingBuffers[NUM_STAGING_BUFFERS];
    std::span<uint8> m_mappedStagingBuffers[NUM_STAGING_BUFFERS];
    vk::Fence m_fences[NUM_STAGING_BUFFERS];
    vk::Semaphore m_semaphores[NUM_STAGING_BUFFERS];
    CommandBuffer m_commandBuffers[NUM_STAGING_BUFFERS];

    vk::Semaphore m_nextUpdateSemaphore = VK_NULL_HANDLE;

    int m_currentBuffer = 0;
    vk::DeviceSize m_currentBufferOffset = 0;
    std::vector<std::pair<vk::Buffer, vk::BufferCopy>> m_bufferCopyRegions;
    std::vector<std::pair<vk::Image, vk::BufferImageCopy>> m_imageCopyRegions;
    struct UploadAndMip
    {
        vk::Image image;
        vk::BufferImageCopy bufferCopy;
        uint32 width, height, numMips;
    };
    std::vector<UploadAndMip> m_imageCopyAndMipList;
    std::span<uint8> m_mappedMemory;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU4")
    StagingManager stagingManager;
#pragma warning(default: 4075)
}
} // extern "C++"