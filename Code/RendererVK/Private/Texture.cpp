module RendererVK.Texture;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.StagingManager;
import RendererVK.stb_image;

Texture::Texture()
{
}

Texture::~Texture()
{
    vk::Device vkDevice = VK::g_dev.getDevice();
    if (m_imageView)
        vkDevice.destroyImageView(m_imageView);
    if (m_imageMemory)
        vkDevice.freeMemory(m_imageMemory);
    if (m_image)
        vkDevice.destroyImage(m_image);
    if (m_imageReadySemaphore)
        vkDevice.destroySemaphore(m_imageReadySemaphore);
}

bool Texture::initialize(StagingManager& stagingManager, const char* pFilePath)
{
    vk::Device vkDevice = VK::g_dev.getDevice();
    vk::SemaphoreTypeCreateInfo semaphoreTypeCreateInfo = { .semaphoreType = vk::SemaphoreType::eBinary };
    m_imageReadySemaphore = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{ .pNext = &semaphoreTypeCreateInfo });

    std::unique_ptr<uint8, void(*)(uint8*)> pData(
        stbi_load(pFilePath, (int*)&m_width, (int*)&m_height, (int*)&m_numChannels, 4),
        [](uint8* p) { if (p) stbi_image_free(p); });

    const vk::DeviceSize imageSize = m_width * m_height * 4;
    if (!pData)
    {
        assert(false && "Failed to load image");
        return false;
    }

    vk::ImageCreateInfo imageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR8G8B8A8Unorm,
        .extent = { m_width, m_height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    m_image = vkDevice.createImage(imageCreateInfo);
    if (!m_image)
    {
        assert(false && "Failed to create image");
        return false;
    }

    vk::MemoryRequirements memoryRequirements = vkDevice.getImageMemoryRequirements(m_image);
    vk::MemoryAllocateInfo memoryAllocateInfo = {
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = VK::g_dev.findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
    };
    m_imageMemory = vkDevice.allocateMemory(memoryAllocateInfo);
    if (!m_imageMemory)
    {
        assert(false && "Failed to allocate memory");
        return false;
    }
    vkDevice.bindImageMemory(m_image, m_imageMemory, 0);

    vk::ImageViewCreateInfo imageViewInfo{
        .image = m_image,
        .viewType = vk::ImageViewType::e2D,
        .format = imageCreateInfo.format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = imageCreateInfo.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = imageCreateInfo.arrayLayers
        }
    };
    m_imageView = vkDevice.createImageView(imageViewInfo);
    if (!m_imageView)
    {
        assert(false && "Failed to create image view");
        return false;
    }
    stagingManager.uploadImage(m_image, m_width, m_height, imageSize, pData.get());
    return true;
}