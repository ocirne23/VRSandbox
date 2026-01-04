export module RendererVK.Framebuffers;
extern "C++" {

import Core;
import RendererVK.VK;

export class RenderPass;
export class SwapChain;

export class Framebuffers final
{
public:

    Framebuffers();
    ~Framebuffers();
    Framebuffers(const Framebuffers&) = delete;

    bool initialize(const RenderPass& renderPass, const SwapChain& swapChain);

    const std::vector<vk::Framebuffer>& getFramebuffers() const { return m_framebuffers; }
    vk::Framebuffer getFramebuffer(int index) { return m_framebuffers[index]; }

private:

    std::vector<vk::Framebuffer> m_framebuffers;
    std::vector<vk::ImageView> m_imageViews;
    vk::DeviceMemory m_depthImageMemory;
    vk::Image m_depthImage;
    vk::ImageView m_depthImageView;
};
} // extern "C++"