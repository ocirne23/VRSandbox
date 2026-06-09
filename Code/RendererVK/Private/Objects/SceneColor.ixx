export module RendererVK:SceneColor;

import Core;
import :VK;

// Offscreen colour+depth target the lit scene renders into (instead of straight to the swapchain), so the
// TAA resolve has an isolated image to accumulate. The colour attachment uses the swapchain surface format
// (keeping the static-mesh / GI-debug pipelines render-pass-compatible with the swapchain pass) and ends the
// pass in SHADER_READ_ONLY for the TAA compute pass to sample. One instance per frame-in-flight (written then
// sampled within the same frame, like GBuffer / ShadowMap).
export class SceneColor final
{
public:
    SceneColor();
    ~SceneColor();
    SceneColor(const SceneColor&) = delete;

    bool initialize(vk::Format colorFormat, uint32 width, uint32 height);
    void destroy();

    vk::RenderPass  getRenderPass() const  { return m_renderPass; }
    vk::Framebuffer getFramebuffer() const { return m_framebuffer; }
    vk::ImageView   getColorView() const   { return m_colorView; }
    vk::Sampler     getSampler() const     { return m_sampler; } // linear, clamp
    uint32 getWidth() const  { return m_width; }
    uint32 getHeight() const { return m_height; }

private:
    uint32 m_width = 0;
    uint32 m_height = 0;
    vk::Format m_colorFormat = vk::Format::eUndefined;

    vk::Image m_colorImage;
    vk::DeviceMemory m_colorMemory;
    vk::ImageView m_colorView;

    vk::Image m_depthImage;
    vk::DeviceMemory m_depthMemory;
    vk::ImageView m_depthView;

    vk::RenderPass m_renderPass;
    vk::Framebuffer m_framebuffer;
    vk::Sampler m_sampler;
};
