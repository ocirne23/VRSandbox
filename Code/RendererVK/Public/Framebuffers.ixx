module;

#include "VK.h"

export module RendererVK.Framebuffers;

export class Device;
export class RenderPass;
export class SwapChain;

export class Framebuffers
{
public:

	Framebuffers();
	~Framebuffers();
	Framebuffers(const Framebuffers&) = delete;

	bool initialize(const Device& device, const RenderPass& renderPass, const SwapChain& swapChain);

	const std::vector<vk::Framebuffer>& getFramebuffers() const { return m_framebuffers; }
	vk::Framebuffer getFramebuffer(int index) { return m_framebuffers[index]; }

private:

	std::vector<vk::Framebuffer> m_framebuffers;
	std::vector<vk::ImageView> m_imageViews;
	vk::DeviceMemory m_depthImageMemory;
	vk::Image m_depthImage;
	vk::ImageView m_depthImageView;
	vk::Device m_device;
};