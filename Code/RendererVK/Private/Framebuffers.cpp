module RendererVK.Framebuffers;

import RendererVK.VK;
import RendererVK.Device;
import RendererVK.RenderPass;
import RendererVK.SwapChain;

Framebuffers::Framebuffers() {}
Framebuffers::~Framebuffers()
{
	vk::Device vkDevice = VK::g_dev.getDevice();

	if (m_depthImageView)
		vkDevice.destroyImageView(m_depthImageView);
	if (m_depthImage)
		vkDevice.destroyImage(m_depthImage);
	if (m_depthImageMemory)
		vkDevice.freeMemory(m_depthImageMemory);
	for (vk::ImageView imageView : m_imageViews)
		vkDevice.destroyImageView(imageView);
	for (vk::Framebuffer framebuffer : m_framebuffers)
		vkDevice.destroyFramebuffer(framebuffer);
}

bool Framebuffers::initialize(const RenderPass& renderPass, const SwapChain& swapChain)
{
	vk::Device vkDevice = VK::g_dev.getDevice();
	std::vector<vk::Image> images = vkDevice.getSwapchainImagesKHR(swapChain.getSwapChain());
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

		vk::ImageView imageView = vkDevice.createImageView(viewInfo);
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
	m_depthImage = vkDevice.createImage(depthImageInfo);
	if (!m_depthImage)
	{
		assert(false && "Could not create a depth image\n");
		return false;
	}

	vk::PhysicalDeviceMemoryProperties memoryProperties = VK::g_dev.getPhysicalDevice().getMemoryProperties();
	vk::MemoryRequirements memoryRequirements = vkDevice.getImageMemoryRequirements(m_depthImage);
	const uint32 typeIndex = VK::g_dev.findMemoryType(memoryRequirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
	vk::MemoryAllocateInfo depthImageAllocInfo {
		.allocationSize = memoryRequirements.size,
		.memoryTypeIndex = typeIndex
	};
	m_depthImageMemory = vkDevice.allocateMemory(depthImageAllocInfo);
	vkDevice.bindImageMemory(m_depthImage, m_depthImageMemory, 0);
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
	m_depthImageView = vkDevice.createImageView(depthImageViewInfo);
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
			.attachmentCount = static_cast<uint32>(attachments.size()),
			.pAttachments = attachments.data(),
			.width = size.width,
			.height = size.height,
			.layers = 1
		};
		vk::Framebuffer framebuffer = vkDevice.createFramebuffer(framebufferInfo);
		if (!framebuffer)
		{
			assert(false && "Could not create a framebuffer\n");
			return false;
		}
		m_framebuffers.push_back(framebuffer);
	}

	return true;
}