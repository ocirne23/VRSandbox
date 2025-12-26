module RendererVK.StagingManager;

import Core;
import RendererVK;
import RendererVK.VK;
import RendererVK.Device;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.SwapChain;

static constexpr size_t STAGING_BUFFER_SIZE = 20 * 1024 * 1024;

StagingManager::StagingManager()
{
}

StagingManager::~StagingManager()
{
    vk::Device vkDevice = Globals::device.getDevice();
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++)
    {
        //m_stagingBuffers[m_currentBuffer].unmapMemory();
        vkDevice.destroyFence(m_fences[i]);
        vkDevice.destroySemaphore(m_semaphores[i]);
    }
}

bool StagingManager::initialize()
{
    vk::Device vkDevice = Globals::device.getDevice();
    for (int i = 0; i < NUM_STAGING_BUFFERS; i++)
    {
        if (!m_stagingBuffers[i].initialize(STAGING_BUFFER_SIZE, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached))
            return false;

        m_commandBuffers[i].initialize(vk::CommandBufferLevel::ePrimary);
        auto createFenceResult = vkDevice.createFence(vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
        if (createFenceResult.result != vk::Result::eSuccess)
        {
            assert(false && "Failed to create fence");
            return false;
        }
        m_fences[i] = createFenceResult.value;

        auto createSemaphoreResult = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{});
        if (createSemaphoreResult.result != vk::Result::eSuccess)
        {
            assert(false && "Failed to create semaphore");
            return false;
        }
        m_semaphores[i] = createSemaphoreResult.value;
        m_mappedStagingBuffers[i] = m_stagingBuffers[i].mapMemory();
    }
    m_mappedMemory = m_mappedStagingBuffers[m_currentBuffer];

    return true;
}

vk::Semaphore StagingManager::upload(vk::Buffer dstBuffer, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset)
{
    assert(dataSize <= m_mappedMemory.size());
    if (m_currentBufferOffset + dataSize > m_mappedMemory.size())
        m_nextUpdateSemaphore = update();

    assert(m_currentBufferOffset + dataSize <= m_mappedMemory.size());
    memcpy(m_mappedMemory.data() + m_currentBufferOffset, data, dataSize);
    m_bufferCopyRegions.emplace_back(std::pair<vk::Buffer, vk::BufferCopy>{ dstBuffer, vk::BufferCopy{ .srcOffset = m_currentBufferOffset, .dstOffset = dstOffset, .size = dataSize } });
    m_currentBufferOffset += dataSize;

    return m_semaphores[m_currentBuffer];
}

vk::Semaphore StagingManager::uploadImage(vk::Image dstImage, uint32 imageWidth, uint32 imageHeight, vk::DeviceSize dataSize, const void* data, uint32 mipLevel, vk::DeviceSize dstOffset)
{
    assert(dataSize <= m_mappedMemory.size());
    if (m_currentBufferOffset + dataSize > m_mappedMemory.size())
        m_nextUpdateSemaphore = update();

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

    assert(m_currentBufferOffset + dataSize <= m_mappedMemory.size());
    memcpy(m_mappedMemory.data() + m_currentBufferOffset, data, dataSize);
    m_imageCopyRegions.emplace_back(std::pair<vk::Image, vk::BufferImageCopy>{ dstImage, bufferImageCopy });
    m_currentBufferOffset += dataSize;

    return m_semaphores[m_currentBuffer];
}

vk::Semaphore StagingManager::uploadImageAndGenerateMipMaps(vk::Image image, uint32 imageWidth, uint32 imageHeight, uint32 numMipLevels, vk::DeviceSize dataSize, const void* data, vk::DeviceSize dstOffset)
{
    assert(dataSize <= m_mappedMemory.size());
    if (m_currentBufferOffset + dataSize > m_mappedMemory.size())
        m_nextUpdateSemaphore = update();

    vk::BufferImageCopy bufferImageCopy{
        .bufferOffset = m_currentBufferOffset,
        .imageSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },
        .imageExtent = { imageWidth, imageHeight, 1 }
    };

    assert(m_currentBufferOffset + dataSize <= m_mappedMemory.size());
    memcpy(m_mappedMemory.data() + m_currentBufferOffset, data, dataSize);
    m_imageCopyAndMipList.emplace_back(image, bufferImageCopy, imageWidth, imageHeight, numMipLevels);
    m_currentBufferOffset += dataSize;

    return m_semaphores[m_currentBuffer];
}

vk::Semaphore StagingManager::update()
{
    if (m_bufferCopyRegions.empty() && m_imageCopyRegions.empty() && m_imageCopyAndMipList.empty())
    {
        vk::Semaphore semaphore = m_nextUpdateSemaphore;
        m_nextUpdateSemaphore = VK_NULL_HANDLE;
		return semaphore;
    }
    vk::Device vkDevice = Globals::device.getDevice();

    vk::Result result = vkDevice.waitForFences(1, &m_fences[m_currentBuffer], vk::True, UINT64_MAX);
    if (result != vk::Result::eSuccess)
        assert(false && "Failed to wait for fence");
    result = vkDevice.resetFences(1, &m_fences[m_currentBuffer]);
    if (result != vk::Result::eSuccess)
        assert(false && "Failed to reset fence");

    const static vk::DeviceSize atomSize = Globals::device.getNonCoherentAtomSize();
    vk::DeviceSize size = (m_currentBufferOffset + atomSize - 1) & ~(atomSize - 1);
    auto flushResult = vkDevice.flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = m_stagingBuffers[m_currentBuffer].getMemory(), .offset = 0, .size = size
    } });
    if (flushResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to flush memory");
    }
    CommandBuffer& commandBuffer = m_commandBuffers[m_currentBuffer];
    if (m_nextUpdateSemaphore)
		commandBuffer.addWaitSemaphore(m_nextUpdateSemaphore, vk::PipelineStageFlagBits::eTransfer);
    commandBuffer.addSignalSemaphore(m_semaphores[m_currentBuffer]);
    vk::CommandBuffer vkCommandBuffer = commandBuffer.begin(true);

    for (const auto& copyRegion : m_bufferCopyRegions)
        vkCommandBuffer.copyBuffer(m_stagingBuffers[m_currentBuffer].getBuffer(), copyRegion.first, 1, &copyRegion.second);

    if (!m_imageTransitions.empty())
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = (uint32)m_imageTransitions.size(), .pImageMemoryBarriers = m_imageTransitions.data() });

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
                .levelCount = 1,
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
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &postCopyBarrier });
    }

    for (const UploadAndMip& mipGen : m_imageCopyAndMipList)
    {
        vk::Image image = mipGen.image;
        vk::BufferImageCopy copyRegion = mipGen.bufferCopy;
        int32 mipWidth = mipGen.width;
        int32 mipHeight = mipGen.height;
        const uint32 numMipLevels = mipGen.numMips;

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
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &preCopyBarrier });
        vkCommandBuffer.copyBufferToImage(m_stagingBuffers[m_currentBuffer].getBuffer(), image, vk::ImageLayout::eTransferDstOptimal, copyRegion);

        vk::ImageMemoryBarrier2 postCopyBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = vk::ImageSubresourceRange
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &postCopyBarrier });

        for (uint32 i = 1; i < numMipLevels; ++i)
        {
            vk::ImageMemoryBarrier2 preBlitBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eNone,
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
                    .baseMipLevel = i,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &preBlitBarrier });

            vk::ImageBlit blit{
                .srcSubresource = vk::ImageSubresourceLayers{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = i - 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .dstSubresource = vk::ImageSubresourceLayers{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .mipLevel = i,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
            };
            blit.srcOffsets[0] = { 0,0,0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.dstOffsets[0] = { 0,0,0 };
            blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };

            vkCommandBuffer.blitImage(
                image, vk::ImageLayout::eTransferSrcOptimal,
                image, vk::ImageLayout::eTransferDstOptimal,
                1, &blit, vk::Filter::eLinear);

            vk::ImageMemoryBarrier2 postBlitBarrier{
                .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
                .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
                .oldLayout = vk::ImageLayout::eTransferDstOptimal,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
                .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
                .image = image,
                .subresourceRange = vk::ImageSubresourceRange
                {
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = i,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };
            vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &postBlitBarrier });

            if (mipWidth > 1) 
                mipWidth /= 2;
            if (mipHeight > 1) 
                mipHeight /= 2;
        }
        vk::ImageMemoryBarrier2 finalTransitionBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = image,
            .subresourceRange = vk::ImageSubresourceRange
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = numMipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &finalTransitionBarrier });
    }

    commandBuffer.end();
    commandBuffer.submitGraphics(m_fences[m_currentBuffer]);

    vk::Semaphore semaphore = m_semaphores[m_currentBuffer];
    m_nextUpdateSemaphore = VK_NULL_HANDLE;

    m_currentBuffer = (m_currentBuffer + 1) % NUM_STAGING_BUFFERS;
    m_currentBufferOffset = 0;
    m_bufferCopyRegions.clear();
    m_imageCopyRegions.clear();
    m_imageCopyAndMipList.clear();
    m_mappedMemory = m_mappedStagingBuffers[m_currentBuffer];
    return semaphore;
}