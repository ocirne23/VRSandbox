module RendererVK.Texture;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.StagingManager;
import RendererVK.stb_image;
import RendererVK.DDS;

Texture::Texture()
{
}

Texture::~Texture()
{
    vk::Device vkDevice = Globals::device.getDevice();
    if (m_imageView)
        vkDevice.destroyImageView(m_imageView);
    if (m_imageMemory)
        vkDevice.freeMemory(m_imageMemory);
    if (m_image)
        vkDevice.destroyImage(m_image);
    if (m_imageReadySemaphore)
        vkDevice.destroySemaphore(m_imageReadySemaphore);
}

bool Texture::initialize(StagingManager& stagingManager, const char* pFilePath, bool sRGB)
{
    vk::Device vkDevice = Globals::device.getDevice();
    vk::SemaphoreTypeCreateInfo semaphoreTypeCreateInfo = { .semaphoreType = vk::SemaphoreType::eBinary };
    auto createSemaphoreResult = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{ .pNext = &semaphoreTypeCreateInfo });
    if (createSemaphoreResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create semaphore");
        return false;
    }
    m_imageReadySemaphore = createSemaphoreResult.value;

    std::filesystem::path path(pFilePath);
    std::vector<uint8> fileData;
    std::vector<std::span<uint8>> imgData; // pixel data per mip level

    if (path.extension() == ".dds")
    {
        FILE* pFile;
        fopen_s(&pFile, pFilePath, "rb");
        std::unique_ptr<FILE, void(*)(FILE*)> file(pFile, [](FILE* p) { if (p) fclose(p); });
        if (!file)
        {
            assert(false && "Failed to open file");
            return false;
        }
        const size_t size = std::filesystem::file_size(pFilePath);
        fileData.resize(size);
        if (fread(fileData.data(), 1, size, file.get()) != size)
        {
            assert(false && "Failed to read file");
            return false;
        }

        dds::Header header = dds::read_header(fileData.data(), size);
        //dds::DXGI_FORMAT format = header.format();

        m_width = header.width();
        m_height = header.height();
        m_numMipLevels = header.mip_levels();
        m_format = sRGB ? vk::Format::eBc1RgbSrgbBlock : vk::Format::eBc1RgbUnormBlock;

        for (uint32 i = 0; i < m_numMipLevels; i++)
        {
            imgData.push_back(std::span(fileData.data() + header.mip_offset(i), header.mip_size(i)));
        }
    }
    else
    {
        assert(false && "reimplement non dds loading");
        /*
        std::unique_ptr<uint8, void(*)(uint8*)> pData(
            stbi_load(pFilePath, (int*)&m_width, (int*)&m_height, (int*)&m_numChannels, 4),
            [](uint8* p) { if (p) stbi_image_free(p); });

        const vk::DeviceSize imageSize = m_width * m_height * 4;
        if (!pData)
        {
            assert(false && "Failed to load image");
            return false;
        } 
        */
    }

    vk::ImageCreateInfo imageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = m_format,
        .extent = { m_width, m_height, 1 },
        .mipLevels = m_numMipLevels,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

    auto createImageResult = vkDevice.createImage(imageCreateInfo);
    if (createImageResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create image");
        return false;
    }
    m_image = createImageResult.value;

    vk::MemoryRequirements memoryRequirements = vkDevice.getImageMemoryRequirements(m_image);
    vk::MemoryAllocateInfo memoryAllocateInfo = {
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = Globals::device.findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
    };
    auto allocateMemoryResult = vkDevice.allocateMemory(memoryAllocateInfo);
    if (allocateMemoryResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to allocate memory");
        return false;
    }
    m_imageMemory = allocateMemoryResult.value;

    auto bindResult = vkDevice.bindImageMemory(m_image, m_imageMemory, 0);
    if (bindResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to bind image memory");
        return false;
    }

    vk::ImageViewCreateInfo imageViewInfo{
        .image = m_image,
        .viewType = vk::ImageViewType::e2D,
        .format = imageCreateInfo.format,
        .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = vk::RemainingMipLevels,
            .baseArrayLayer = 0,
            .layerCount = imageCreateInfo.arrayLayers
        }
    };

    auto createImageViewResult = vkDevice.createImageView(imageViewInfo);
    if (createImageViewResult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to create image view");
        return false;
    }
    m_imageView = createImageViewResult.value;

    stagingManager.transitionImage(vk::ImageMemoryBarrier2{
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            .image = m_image,
            .subresourceRange = vk::ImageSubresourceRange
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = m_numMipLevels,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        });

#if 1
    for (uint32 i = 0; i < m_numMipLevels; i++)
    {
        stagingManager.uploadImage(m_image, m_width >> i, m_height >> i, imgData[i].size(), imgData[i].data(), i);
    }
#else
    for (uint32 i = m_numMipLevels - 1; i > 5 - 2; i--)
    {
        stagingManager.uploadImage(m_image, m_width >> i, m_height >> i, imgData[i].size(), imgData[i].data(), i);
    }
#endif
    return true;
}