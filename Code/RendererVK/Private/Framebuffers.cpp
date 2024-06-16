module;

#include "VK.h"

module RendererVK.Framebuffers;

import RendererVK.Device;
import RendererVK.RenderPass;
import RendererVK.SwapChain;

Framebuffers::Framebuffers() {}
Framebuffers::~Framebuffers()
{
	if (m_depthImageView)
		m_device.destroyImageView(m_depthImageView);
	if (m_depthImage)
		m_device.destroyImage(m_depthImage);
	if (m_depthImageMemory)
		m_device.freeMemory(m_depthImageMemory);
	for (vk::ImageView imageView : m_imageViews)
		m_device.destroyImageView(imageView);
	for (vk::Framebuffer framebuffer : m_framebuffers)
		m_device.destroyFramebuffer(framebuffer);
}

bool Framebuffers::initialize(const Device& device, const RenderPass& renderPass, const SwapChain& swapChain)
{
	m_device = device.getDevice();
	std::vector<vk::Image> images = m_device.getSwapchainImagesKHR(swapChain.getSwapChain());
	m_imageViews.reserve(images.size());
	for (const vk::Image& image : images)
	{
		vk::ImageViewCreateInfo viewInfo =
		{
			.image = image,
			.viewType = vk::ImageViewType::e2D,
			.format = swapChain.getLayout().format,
			.components = {},
			.subresourceRange =
			{
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1
			}
		};

		vk::ImageView imageView = m_device.createImageView(viewInfo);
		if (!imageView)
		{
			assert(false && "Could not create an image view\n");
			return false;
		}
		m_imageViews.push_back(imageView);
	}
	const vk::Extent2D size = swapChain.getLayout().extent;

	vk::ImageCreateInfo depthImageInfo =
	{
		.imageType = vk::ImageType::e2D,
		.format = vk::Format::eD32Sfloat,
		.extent = { size.width, size.height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = vk::ImageTiling::eOptimal,
		.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
		.sharingMode = vk::SharingMode::eExclusive,
		.initialLayout = vk::ImageLayout::eUndefined,
	};
	m_depthImage = m_device.createImage(depthImageInfo);
	if (!m_depthImage)
	{
		assert(false && "Could not create a depth image\n");
		return false;
	}

	vk::PhysicalDeviceMemoryProperties memoryProperties = device.getPhysicalDevice().getMemoryProperties();
	vk::MemoryRequirements memoryRequirements = m_device.getImageMemoryRequirements(m_depthImage);
	const uint32_t typeIndex = device.findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	vk::MemoryAllocateInfo depthImageAllocInfo {
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = typeIndex
	};
	m_depthImageMemory = m_device.allocateMemory(depthImageAllocInfo);
	m_device.bindImageMemory(m_depthImage, m_depthImageMemory, 0);
	vk::ImageViewCreateInfo depthImageViewInfo {
		.image = m_depthImage,
		.viewType = vk::ImageViewType::e2D,
		.format = depthImageInfo.format,
		.components = {},
		.subresourceRange =
		{
			.aspectMask = vk::ImageAspectFlagBits::eDepth,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1
		}
	};
	m_depthImageView = m_device.createImageView(depthImageViewInfo);
	if (!m_depthImageView)
	{
		assert(false && "Could not create a depth image view\n");
		return false;
	}

	std::array<vk::ImageView, 2> attachments;
	attachments[1] = m_depthImageView;
	m_framebuffers.reserve(m_imageViews.size());
	for (size_t i = 0; i < m_imageViews.size(); i++)
	{
		attachments[0] = m_imageViews[i];
		vk::FramebufferCreateInfo framebufferInfo =
		{
			.renderPass = renderPass.getRenderPass(),
			.attachmentCount = static_cast<uint32_t>(attachments.size()),
			.pAttachments = attachments.data(),
			.width = size.width,
			.height = size.height,
			.layers = 1
		};
		vk::Framebuffer framebuffer = m_device.createFramebuffer(framebufferInfo);
		if (!framebuffer)
		{
			assert(false && "Could not create a framebuffer\n");
			return false;
		}
		m_framebuffers.push_back(framebuffer);
	}

	return true;
}