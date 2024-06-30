export module RendererVK.SwapChain;

import Core;
import RendererVK.VK;
import RendererVK.CommandBuffer;

export class Device;
export class Surface;

export class SwapChain final
{
public:

	struct Layout
	{
		uint32_t numImages;
		vk::Format format;
		vk::ColorSpaceKHR colorSpace;
		vk::Extent2D extent;
	};

	struct SyncObjects
	{
		vk::Semaphore imageAvailable;
		vk::Semaphore renderFinished;
		vk::Fence inFlight;
	};

	SwapChain();
	~SwapChain();
	SwapChain(const SwapChain&) = delete;

	bool initialize(const Device& device, const Surface& surface, uint32_t swapChainSize);
	uint32_t acquireNextImage();
	bool present(uint32_t imageIdx);

	vk::SwapchainKHR getSwapChain() const { return m_swapChain; }
	const Layout& getLayout() const { return m_layout; }
	CommandBuffer& getCurrentCommandBuffer() { return m_commandBuffers[m_currentFrame]; }
	uint32 getCurrentFrameIndex() const { return m_currentFrame; }

private:

	vk::SwapchainKHR m_swapChain;
	Layout m_layout;
	const Device* m_device = nullptr;

	uint32_t m_currentFrame = 0;
	std::vector<CommandBuffer> m_commandBuffers;
	std::vector<SyncObjects> m_syncObjects;
};