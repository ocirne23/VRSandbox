export module RendererVK:GBuffer;

import Core;
import :VK;
import :Allocator;
import :Layout;

// Thin G-buffer used to drive screen-space ray-traced AO (and, later, other screen-space effects).
// One color attachment storing the world-space shading normal (RGBA16F; .a unused for now) plus a
// D32 depth attachment. Both attachments end the prepass in SHADER_READ_ONLY so the RTAO compute
// pass can sample them. Sized to the swapchain extent; one instance per frame-in-flight (written and
// then sampled within the same frame, like ShadowMap).
export class GBuffer final
{
public:
    GBuffer();
    ~GBuffer();
    GBuffer(const GBuffer&) = delete;

    bool initialize(uint32 width, uint32 height);
    void destroy();

    vk::RenderPass getRenderPass() const  { return m_renderPass; }
    vk::Framebuffer getFramebuffer() const { return m_framebuffer; }
    vk::ImageView getNormalView() const   { return m_normalView; }
    vk::ImageView getDepthView() const    { return m_depthView; }
    vk::Sampler getSampler() const        { return m_sampler; } // nearest, clamp (point sampling for reconstruction)
    uint32 getWidth() const  { return m_width; }
    uint32 getHeight() const { return m_height; }

private:
    uint32 m_width = 0;
    uint32 m_height = 0;

    vk::Image m_normalImage;
    VmaAllocation m_normalMemory = nullptr;
    vk::ImageView m_normalView;

    vk::Image m_depthImage;
    VmaAllocation m_depthMemory = nullptr;
    vk::ImageView m_depthView;

    vk::RenderPass m_renderPass;
    vk::Framebuffer m_framebuffer;
    vk::Sampler m_sampler;
};
