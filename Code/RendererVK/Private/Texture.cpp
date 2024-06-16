module;

#include "VK.h"

#include <stb/stb_image.h>

module RendererVK.Texture;

import RendererVK.Buffer;
import RendererVK.CommandBuffer;

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

bool Texture::initialize(Device& device, CommandBuffer& commandBuffer, const char* pFilePath)
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
		return false;
	}
	m_device.bindImageMemory(m_image, m_imageMemory, 0);
	/*
	const vk::DeviceSize nomCoherentAtomSize = device.getPhysicalDevice().getProperties().limits.nonCoherentAtomSize;
	const vk::DeviceSize alignedSize = (imageSize - 1) - ((imageSize - 1) % nomCoherentAtomSize) + nomCoherentAtomSize;
	vk::MappedMemoryRange mappedMemoryRange = {
		.memory = m_imageMemory,
		.offset = 0,
		.size = alignedSize
	};
	m_device.flushMappedMemoryRanges(mappedMemoryRange);*/

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
		return false;
	}

	Buffer stagingBuffer;
	stagingBuffer.initialize(device, imageSize, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	std::span<uint8_t> pMem = stagingBuffer.mapMemory();
	memcpy(pMem.data(), pData, imageSize);
	stagingBuffer.unmapMemory();

	stbi_image_free(pData);

	vk::ImageMemoryBarrier2 preCopyBarrier {
		.dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
		.dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
		.oldLayout = vk::ImageLayout::eUndefined,
		.newLayout = vk::ImageLayout::eTransferDstOptimal,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = m_image,
		.subresourceRange = vk::ImageSubresourceRange
		{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vk::DependencyInfo dependencyInfo {
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &preCopyBarrier
	};
	vk::BufferImageCopy bufferImageCopy {
		.bufferOffset = 0,
		.imageSubresource = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.mipLevel = 0,
			.baseArrayLayer = 0,
			.layerCount = 1
		},
		.imageExtent = { m_width, m_height, 1 }
	};

	vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
	vkCommandBuffer.reset();
	vkCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	vkCommandBuffer.pipelineBarrier2(dependencyInfo);
	vkCommandBuffer.copyBufferToImage(stagingBuffer.getBuffer(), m_image, vk::ImageLayout::eTransferDstOptimal, bufferImageCopy);
	vk::ImageMemoryBarrier2 postCopyBarrier {
		.srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
		.srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
		.oldLayout = vk::ImageLayout::eTransferDstOptimal,
		.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = m_image,
		.subresourceRange = vk::ImageSubresourceRange
		{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	vk::DependencyInfo postCopyDependencyInfo{
		.imageMemoryBarrierCount = 1,
		.pImageMemoryBarriers = &postCopyBarrier
	};
	vkCommandBuffer.pipelineBarrier2(postCopyDependencyInfo);
	vkCommandBuffer.end();
	commandBuffer.addSignalSemaphore(m_imageReadySemaphore);
	vk::Fence fence = m_device.createFence(vk::FenceCreateInfo{});
	commandBuffer.submitGraphics(fence);
	vk::Result result = m_device.waitForFences(fence, VK_TRUE, UINT64_MAX);
	m_device.destroyFence(fence);
	return true;
}