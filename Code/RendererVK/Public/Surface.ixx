export module RendererVK.Surface;

import RendererVK.VK;

export class Instance;
export class Device;
export class Window;

export class Surface
{
public:
	Surface();
	~Surface();
	Surface(const Surface&) = delete;

	bool initialize(const Instance& instance, const Window& window);

	bool deviceSupportsSurface(const Device& device) const;
	vk::SurfaceKHR getSurface() const { return m_surface; }

private:

	vk::SurfaceKHR m_surface;
	vk::Instance m_instance;
};