module;

#include <stb/stb_image.h>

module RendererVK.Texture;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.StagingManager;

Texture::Texture()
{
}

Texture::~Texture()
{
	if (m_imageView)
		m_device.destroyImageView(m_imageView);
	if (m_imageMemory)
		m_device.freeMemory(m_imageMemory);
	if (m_image)
		m_device.destroyImage(m_image);
}

bool Texture::initialize(Device& device, StagingManager& stagingManager, const char* pFilePath)
{
	vk::SemaphoreTypeCreateInfo semaphoreTypeCreateInfo = { .semaphoreType = vk::SemaphoreType::eBinary };
	vk::SemaphoreCreateInfo semaphoreCreateInfo = { .pNext = &semaphoreTypeCreateInfo };
	m_imageReadySemaphore = device.getDevice().createSemaphore(semaphoreCreateInfo);
	m_device = device.getDevice();
	uint8_t* pData = stbi_load(pFilePath, (int*)&m_width, (int*)&m_height, (int*)&m_numChannels, 4);
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

	m_image = m_device.createImage(imageCreateInfo);
	if (!m_image)
	{
		assert(false && "Failed to create image");
		stbi_image_free(pData);
		return false;
	}

	vk::MemoryRequirements memoryRequirements = m_device.getImageMemoryRequirements(m_image);
	vk::MemoryAllocateInfo memoryAllocateInfo = {
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = device.findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)
	};
	m_imageMemory = m_device.allocateMemory(memoryAllocateInfo);
	if (!m_imageMemory)
	{
		assert(false && "Failed to allocate memory");
		stbi_image_free(pData);
		return false;
	}
	m_device.bindImageMemory(m_image, m_imageMemory, 0);

	vk::ImageViewCreateInfo imageViewInfo {
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
	m_imageView = m_device.createImageView(imageViewInfo);
	if (!m_imageView)
	{
		assert(false && "Failed to create image view");
		stbi_image_free(pData);
		return false;
	}
	stagingManager.uploadImage(m_image, m_width, m_height, imageSize, pData);
	stbi_image_free(pData);
	return true;
}