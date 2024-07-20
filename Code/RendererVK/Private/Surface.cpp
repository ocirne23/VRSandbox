module RendererVK.Surface;

import Core;
import Core.SDL;
import RendererVK.VK;
import Core.Window;
import RendererVK.Instance;
import RendererVK.Device;

Surface::Surface() {}
Surface::~Surface() 
{
	if (m_surface)
	{
		VK::g_inst.getInstance().destroySurfaceKHR(m_surface);
	}
}

bool Surface::initialize(const Window& window)
{
	VkSurfaceKHR vkSurface = nullptr;
	if (SDL_Vulkan_CreateSurface((SDL_Window*)window.getWindowHandle(), VK::g_inst.getInstance(), nullptr, &vkSurface) != 1
		|| vkSurface == nullptr)
	{
		printf("Failed to create Vulkan surface: %s\n", SDL_GetError());
		assert(false);
		return false;
	}
	m_surface = vkSurface;
	return true;
}

bool Surface::deviceSupportsSurface() const
{
	vk::PhysicalDevice physicalDevice = VK::g_dev.getPhysicalDevice();
	std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
	if (!physicalDevice.getSurfaceSupportKHR(VK::g_dev.getGraphicsQueueIndex(), m_surface))
	{
		assert(false && "Separate present queue not supported!");
		return false;
	}
	return true;
}
