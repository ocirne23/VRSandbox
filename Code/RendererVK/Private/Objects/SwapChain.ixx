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
        uint32 numImages;
        vk::SurfaceFormatKHR surfaceFormat;
        vk::Extent2D extent;
        vk::PresentModeKHR presentMode;
    };

    struct SyncObjects
    {
        vk::Semaphore imageAvailable;
        vk::Semaphore renderFinished;
        vk::Fence inFlight;
        bool isInFlight = false;
    };

    SwapChain();
    ~SwapChain();
    SwapChain(const SwapChain&) = delete;

    bool initialize(const Surface& surface, uint32 swapChainSize);

    vk::SwapchainKHR getSwapChain() const { return m_swapChain; }
    const Layout& getLayout() const { return m_layout; }
    uint32 getCurrentFrameIndex() const { return m_currentFrame; }

    void acquireNextImage();
    bool present();
    void waitForFrame(uint32 frameIdx);
    void submitCommandBuffer(CommandBuffer& commandBuffer);

private:

    vk::SwapchainKHR m_swapChain;
    Layout m_layout;

    uint32 m_currentFrame = 0;
    uint32 m_currentImageIdx = 0;
    std::vector<SyncObjects> m_syncObjects;
};