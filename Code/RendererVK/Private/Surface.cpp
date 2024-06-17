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
	if (m_instance && m_surface)
	{
		m_instance.destroySurfaceKHR(m_surface);
	}
}

bool Surface::initialize(const Instance& instance, const Window& window)
{
	m_instance = instance.getHandle();
	VkSurfaceKHR vkSurface = nullptr;
	if (SDL_Vulkan_CreateSurface((SDL_Window*)window.getWindowHandle(), instance.getHandle(), nullptr, &vkSurface) != 1 
		|| vkSurface == nullptr)
	{
		printf("Failed to create Vulkan surface: %s\n", SDL_GetError());
		assert(false);
		return false;
	}
	m_surface = vkSurface;
	return true;
}

bool Surface::deviceSupportsSurface(const Device& device) const
{
	vk::PhysicalDevice physicalDevice = device.getPhysicalDevice();
	std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
	if (!physicalDevice.getSurfaceSupportKHR(device.getGraphicsQueueIndex(), m_surface))
	{
		assert(false && "Separate present queue not supported!");
		return false;
	}
	return true;
}
