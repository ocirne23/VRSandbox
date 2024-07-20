module RendererVK.SwapChain;

import RendererVK.VK;
import RendererVK.Device;
import RendererVK.Surface;
import RendererVK.CommandBuffer;

SwapChain::SwapChain() {}
SwapChain::~SwapChain()
{
    vk::Device vkDevice = VK::g_dev.getDevice();
    for (SyncObjects& syncObjects : m_syncObjects)
    {
        vkDevice.destroySemaphore(syncObjects.imageAvailable);
        vkDevice.destroySemaphore(syncObjects.renderFinished);
        vkDevice.destroyFence(syncObjects.inFlight);
    }
    vkDevice.destroySwapchainKHR(m_swapChain);
}

bool SwapChain::initialize(const Surface& surface, uint32 swapChainSize)
{
    vk::Device vkDevice = VK::g_dev.getDevice();
    vk::PhysicalDevice vkPhysicalDevice = VK::g_dev.getPhysicalDevice();
    vk::SurfaceKHR vkSurface = surface.getSurface();

    vk::SurfaceCapabilitiesKHR capabilities = vkPhysicalDevice.getSurfaceCapabilitiesKHR(vkSurface);
    std::vector<vk::PresentModeKHR> presentModes = vkPhysicalDevice.getSurfacePresentModesKHR(vkSurface);
    std::vector<vk::SurfaceFormatKHR> surfaceFormats = vkPhysicalDevice.getSurfaceFormatsKHR(vkSurface);

    vk::SurfaceFormatKHR surfaceFormat = surfaceFormats[0];
    for (const auto& availableFormat : surfaceFormats)
    {
        if (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
        {
            surfaceFormat = availableFormat;
            break;
        }
    }
    vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

    const uint32 imageCount = std::max(capabilities.minImageCount, std::min(swapChainSize, capabilities.maxImageCount));
	m_commandBuffers.resize(imageCount);
	m_commandBuffers.shrink_to_fit();
	m_syncObjects.resize(imageCount);
	m_syncObjects.shrink_to_fit();

    for (uint32 i = 0; i < imageCount; i++)
    {
        m_commandBuffers[i].initialize();
		m_syncObjects[i].imageAvailable = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{});
		m_syncObjects[i].renderFinished = vkDevice.createSemaphore(vk::SemaphoreCreateInfo{});
		m_syncObjects[i].inFlight = vkDevice.createFence(vk::FenceCreateInfo{ .flags = vk::FenceCreateFlagBits::eSignaled });
    }

    vk::SwapchainCreateInfoKHR createInfo{
        .surface = vkSurface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = capabilities.currentExtent,
        .imageArrayLayers = 1,// capabilities.maxImageArrayLayers,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = compositeAlpha,
        .presentMode = vk::PresentModeKHR::eImmediate,
        .clipped = vk::True,
    };

    m_layout.numImages = imageCount;
    m_layout.format = surfaceFormat.format;
    m_layout.colorSpace = surfaceFormat.colorSpace;
    m_layout.extent = capabilities.currentExtent;

    m_swapChain = vkDevice.createSwapchainKHR(createInfo);
    if (!m_swapChain)
    {
        assert(false && "Failed to create swap chain");
        return false;
    }

    return true;
}

void SwapChain::acquireNextImage()
{
    vk::Device vkDevice = VK::g_dev.getDevice();
	SyncObjects& syncObjects = m_syncObjects[m_currentFrame];
    
    vk::Result result = vkDevice.waitForFences(1, &syncObjects.inFlight, vk::True, UINT64_MAX);
    if (result != vk::Result::eSuccess)
		assert(false && "Failed to wait for fence");
    result = vkDevice.resetFences(1, &syncObjects.inFlight);
    if (result != vk::Result::eSuccess)
		assert(false && "Failed to reset fence");

    vk::ResultValue<uint32> imageIdx = vkDevice.acquireNextImageKHR(m_swapChain, UINT64_MAX, syncObjects.imageAvailable);
    assert(imageIdx.result == vk::Result::eSuccess);
    m_currentImageIdx = imageIdx.value;
}

bool SwapChain::present()
{
    vk::Device vkDevice = VK::g_dev.getDevice();

    vk::PipelineStageFlags2 waitStage = { vk::PipelineStageFlagBits2::eColorAttachmentOutput };
	SyncObjects& syncObjects = m_syncObjects[m_currentFrame];

    CommandBuffer& commandBuffer = m_commandBuffers[m_currentFrame];
    commandBuffer.setWaitStage(waitStage);
    commandBuffer.addWaitSemaphore(syncObjects.imageAvailable, vk::PipelineStageFlagBits::eColorAttachmentOutput);
    commandBuffer.addSignalSemaphore(syncObjects.renderFinished);
    commandBuffer.submitGraphics(syncObjects.inFlight);
    vk::PresentInfoKHR presentInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &syncObjects.renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &m_swapChain,
        .pImageIndices = &m_currentImageIdx,
    };
    vk::Result result = VK::g_dev.getGraphicsQueue().presentKHR(presentInfo);
    switch (result)
    {
    case vk::Result::eSuccess:
        break;
    default:
        assert(false && "Failed to present image");
        return false;
    }
    m_currentFrame = (m_currentFrame + 1) % m_layout.numImages;
    return true;
}