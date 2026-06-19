export module RendererVK.Surface;

import Core.fwd;
import RendererVK.fwd;
import RendererVK.VK;

export class Surface final
{
public:
    Surface();
    ~Surface();
    Surface(const Surface&) = delete;

    bool initialize(const Window& window);

    bool deviceSupportsSurface() const;
    vk::SurfaceKHR getSurface() const { return m_surface; }

private:

    vk::SurfaceKHR m_surface;
    vk::Instance m_instance;
};