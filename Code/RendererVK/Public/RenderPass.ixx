export module RendererVK.RenderPass;

import RendererVK.VK;

export class Device;
export class SwapChain;

export class RenderPass
{
public:
	RenderPass();
	~RenderPass();
	RenderPass(const RenderPass&) = delete;

	bool initialize(const Device& device, const SwapChain& swapChain);

	vk::RenderPass getRenderPass() const { return m_renderPass; }

private:

	vk::RenderPass m_renderPass;
	vk::Device m_device;
};