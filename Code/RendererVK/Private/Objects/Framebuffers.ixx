export module RendererVK.Framebuffers;

import Core;
import RendererVK.fwd;
import RendererVK.VK;
import RendererVK.Allocator;

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
    VmaAllocation m_depthImageMemory = nullptr;
    vk::Image m_depthImage;
    vk::ImageView m_depthImageView;
};