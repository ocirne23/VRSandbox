export module RendererVK.RenderPass;
extern "C++" {

import RendererVK.VK;

export class SwapChain;

export class RenderPass final
{
public:
    RenderPass();
    ~RenderPass();
    RenderPass(const RenderPass&) = delete;

    bool initialize(const SwapChain& swapChain);

    vk::RenderPass getRenderPass() const { return m_renderPass; }

private:

    vk::RenderPass m_renderPass;
};
} // extern "C++"