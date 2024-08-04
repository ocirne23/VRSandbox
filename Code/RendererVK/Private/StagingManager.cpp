module RendererVK.StagingManager;

import Core;
import RendererVK.VK;
import RendererVK.Device;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.SwapChain;

static constexpr size_t STAGING_BUFFER_SIZE = 5 * 1024 * 1024;

StagingManager::StagingManager()
{
}

StagingManager::~StagingManager()
{
    vk::Device vkDevice = VK::g_dev.getDevice();
    m_stagingBuffers[m_currentBuffer].unmapMemory();
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++)
    {
        vkDevice.destroyFence(m_fences[i]);
        vkDevice.destroySemaphore(m_semaphores[i]);
    }
}

bool StagingManager::initialize(SwapChain& swapChain)
{
    m_swapChain = &swapChain;

    vk::Device vkDevice = VK::g_dev.getDevice();
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++)
    {
        if (!m_stagingBuffers[i].initialize(STAGING_BUFFER_SIZE, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
            return false;

        m_commandBuffers[i].initialize();

        vk::FenceCreateInfo fenceCreateInfo = { .flags = vk::FenceCreateFlagBits::eSignaled };
        m_fences[i] = vkDevice.createFence(fenceCreateInfo);

        vk::SemaphoreCreateInfo semaphoreCreateInfo = {};
        m_semaphores[i] = vkDevice.createSemaphore(semaphoreCreateInfo);
    }
    m_mappedMemory = m_stagingBuffers[m_currentBuffer].mapMemory().data();

    return true;
}

vk::Semaphore StagingManager::upload(vk::Buffer dstBuffer, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset)
{
    assert(dataSize <= STAGING_BUFFER_SIZE);
    if (m_currentBufferOffset + dataSize > STAGING_BUFFER_SIZE)
        update();

    memcpy(m_mappedMemory + m_currentBufferOffset, data, dataSize);
    m_bufferCopyRegions.emplace_back(std::pair<vk::Buffer, vk::BufferCopy>{ dstBuffer, vk::BufferCopy{ .srcOffset = m_currentBufferOffset, .dstOffset = dstOffset, .size = dataSize } });
    m_currentBufferOffset += dataSize;

    return m_semaphores[m_currentBuffer];
}

vk::Semaphore StagingManager::uploadImage(vk::Image dstImage, uint32 imageWidth, uint32 imageHeight, vk::DeviceSize dataSize, const void* data, uint32 mipLevel, vk::DeviceSize dstOffset)
{
    assert(dataSize <= STAGING_BUFFER_SIZE);
    if (m_currentBufferOffset + dataSize > STAGING_BUFFER_SIZE)
        update();

    vk::BufferImageCopy bufferImageCopy{
        .bufferOffset = m_currentBufferOffset,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = mipLevel,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageExtent = { imageWidth, imageHeight, 1 }
    };
    memcpy(m_mappedMemory + m_currentBufferOffset, data, dataSize);
    m_imageCopyRegions.emplace_back(std::pair<vk::Image, vk::BufferImageCopy>{ dstImage, bufferImageCopy });
    m_currentBufferOffset += dataSize;

    return m_semaphores[m_currentBuffer];
}

void StagingManager::update()
{
    if (m_bufferCopyRegions.empty() && m_imageCopyRegions.empty())
        return;

    m_stagingBuffers[m_currentBuffer].unmapMemory();

    vk::Device vkDevice = VK::g_dev.getDevice();
    vk::Result result = vkDevice.waitForFences(1, &m_fences[m_currentBuffer], vk::True, UINT64_MAX);
    if (result != vk::Result::eSuccess)
        assert(false && "Failed to wait for fence");
    result = vkDevice.resetFences(1, &m_fences[m_currentBuffer]);
    if (result != vk::Result::eSuccess)
        assert(false && "Failed to reset fence");

    CommandBuffer& commandBuffer = m_commandBuffers[m_currentBuffer];
    commandBuffer.addSignalSemaphore(m_semaphores[m_currentBuffer]);
    vk::CommandBuffer vkCommandBuffer = commandBuffer.begin(true);

    for (const auto& copyRegion : m_bufferCopyRegions)
        vkCommandBuffer.copyBuffer(m_stagingBuffers[m_currentBuffer].getBuffer(), copyRegion.first, 1, &copyRegion.second);

    for (const auto& [image, copyRegion] : m_imageCopyRegions)
    {
        vk::ImageMemoryBarrier2 preCopyBarrier{
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = vk::ImageSubresourceRange
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = copyRegion.imageSubresource.mipLevel,
                .levelCount = vk::RemainingMipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &preCopyBarrier });
        vkCommandBuffer.copyBufferToImage(m_stagingBuffers[m_currentBuffer].getBuffer(), image, vk::ImageLayout::eTransferDstOptimal, copyRegion);
        vk::ImageMemoryBarrier2 postCopyBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = vk::ImageSubresourceRange
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = copyRegion.imageSubresource.mipLevel,
                .levelCount = vk::RemainingMipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &postCopyBarrier });
    }

    commandBuffer.end();
    commandBuffer.submitGraphics(m_fences[m_currentBuffer]);

    m_swapChain->getCurrentCommandBuffer().addWaitSemaphore(m_semaphores[m_currentBuffer], vk::PipelineStageFlagBits::eVertexInput);

    m_currentBuffer = (m_currentBuffer + 1) % NUM_STAGING_BUFFERS;
    m_currentBufferOffset = 0;
    m_bufferCopyRegions.clear();
    m_imageCopyRegions.clear();
    m_mappedMemory = m_stagingBuffers[m_currentBuffer].mapMemory().data();
}