module RendererVK.Texture;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.StagingManager;
import RendererVK.stb_image;
import RendererVK.DDS;
import File.TextureData;

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

Texture::Texture(Texture&& move)
{
    m_width = move.m_width;
    m_height = move.m_height;
    m_numMipLevels = move.m_numMipLevels;
    m_format = move.m_format;
    m_image = move.m_image;
    m_imageMemory = move.m_imageMemory;
    m_imageView = move.m_imageView;
    m_imageReadySemaphore = move.m_imageReadySemaphore;
    move.m_image = VK_NULL_HANDLE;
    move.m_imageMemory = VK_NULL_HANDLE;
    move.m_imageView = VK_NULL_HANDLE;
    move.m_imageReadySemaphore = VK_NULL_HANDLE;
}

bool Texture::initialize(const char* filePath)
{
    std::filesystem::path path(filePath);
    std::vector<uint8> fileData;
    std::vector<std::span<uint8>> imgData; // pixel data per mip level

	bool sRGB = false;
    uint32 width = 0;
    uint32 height = 0;
	vk::Format format = vk::Format::eUndefined;
	uint32 numMipLevels = 0;

    if (path.extension() == ".dds")
    {
        FILE* pFile;
        fopen_s(&pFile, filePath, "rb");
        std::unique_ptr<FILE, void(*)(FILE*)> file(pFile, [](FILE* p) { if (p) fclose(p); });
        if (!file)
        {
            assert(false && "Failed to open file");
            return false;
        }
        const size_t size = std::filesystem::file_size(filePath);
        fileData.resize(size);
        if (fread(fileData.data(), 1, size, file.get()) != size)
        {
            assert(false && "Failed to read file");
            return false;
        }

        dds::Header header = dds::read_header(fileData.data(), size);

        width = header.width();
        height = header.height();
        numMipLevels = header.mip_levels();
        //dds::DXGI_FORMAT format = header.format();
        format = sRGB ? vk::Format::eBc1RgbSrgbBlock : vk::Format::eBc1RgbUnormBlock;

        for (uint32 i = 0; i < numMipLevels; i++)
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

	return initialize(width, height, format, imgData);
}

bool Texture::initialize(const TextureData& textureData)
{
    const bool sRGB = false;
    std::string formatInfo = textureData.getFormatInfo();
    
    int width = textureData.getWidth();
    int height = textureData.getHeight();
    vk::Format format = vk::Format::eUndefined;

    std::vector<std::span<uint8>> imgData;
    
    if (height == 0)
    {
        if (formatInfo.compare("png") == 0)
        {
			const int bufferSize = textureData.getWidth();
            int numComponents = 0;
            int desiredComponents = 1;
			stbi_info_from_memory((stbi_uc*)textureData.getPixels(), bufferSize, &width, &height, &desiredComponents);
            if (desiredComponents == 3)
				desiredComponents = 4; // force 4 component for rgb pngs
            stbi_uc* pixelData = stbi_load_from_memory((stbi_uc*)textureData.getPixels(), bufferSize, &width, &height, &numComponents, desiredComponents);
            if (!pixelData)
            {
                assert(false && "Failed to load png image");
                return false;
			}
            imgData.push_back(std::span((uint8*)pixelData, width * height * desiredComponents));
            switch (desiredComponents)
            {
            case 1:
                format = sRGB ? vk::Format::eR8G8B8Snorm : vk::Format::eR8Unorm;
                break;
            case 2:
				format = sRGB ? vk::Format::eR8G8Snorm : vk::Format::eR8G8Unorm;
				break;
			case 3:
				format = sRGB ? vk::Format::eR8G8B8Snorm : vk::Format::eR8G8B8Unorm;
				break;
			case 4:
				format = sRGB ? vk::Format::eR8G8B8A8Snorm : vk::Format::eR8G8B8A8Unorm;
				break;
            default:
                assert(false && "unsupported number of components in png");
				return false;
            }
        }
        else if (formatInfo.compare("jpg") == 0)
        {
            assert(false && "implement jpg loading");
        }
        else
        {
            assert(false && "unsupported compressed texture format");
        }
    }
    else
    {
        if (formatInfo.compare("argb8888") == 0)
        {
			assert(false && "implement argb8888 loading");
        }
        else if (formatInfo.compare("rgba8888") == 0)
        {
            assert(false && "implement rgba8888 loading");
        }
        else
        {
            assert(false && "unsupported uncompressed texture format");
        }
		//imgData.push_back(std::span((uint8*)pixels, width * height * 4));
    }
    
    return initialize(width, height, format, imgData);
}

bool Texture::initialize(uint32 width, uint32 height, vk::Format format, const std::vector<std::span<uint8>>& imageDataMips)
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
	m_width = width;
	m_height = height;
	m_format = format;
	m_numMipLevels = (uint32)imageDataMips.size();

    vk::ImageCreateInfo imageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = m_format,
        .extent = { m_width, m_height, 1 },
        .mipLevels = m_numMipLevels,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        //.tiling = vk::ImageTiling::eLinear,
        .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined
    };

	auto imageFormatPropertiesresult = Globals::device.getPhysicalDevice().getImageFormatProperties(m_format, imageCreateInfo.imageType, imageCreateInfo.tiling, imageCreateInfo.usage);
    if (imageFormatPropertiesresult.result != vk::Result::eSuccess)
    {
        assert(false && "Failed to get image format properties");
        return false;
	}
	auto imageFormatProperties = imageFormatPropertiesresult.value;

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

    Globals::stagingManager.transitionImage(vk::ImageMemoryBarrier2{
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
        Globals::stagingManager.uploadImage(m_image, m_width >> i, m_height >> i, imageDataMips[i].size(), imageDataMips[i].data(), i);
    }
#else
    for (uint32 i = m_numMipLevels - 1; i > 5 - 2; i--)
    {
        stagingManager.uploadImage(m_image, m_width >> i, m_height >> i, imageDataMips[i].size(), imageDataMips[i].data(), i);
    }
#endif
    return true;
}