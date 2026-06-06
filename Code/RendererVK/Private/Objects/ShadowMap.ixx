export module RendererVK:ShadowMap;

import Core;
import :VK;
import :Layout;

// Cascaded shadow map render target: a single D32 depth image with one array layer per cascade,
// a depth-only render pass, one framebuffer per cascade layer, and a comparison sampler for PCF
// lookups in the lighting shader. Designed so additional shadow-casting lights can allocate their
// own ShadowMap instances later.
export class ShadowMap final
{
public:
    ShadowMap();
    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;

    bool initialize(uint32 resolution = RendererVKLayout::SHADOW_MAP_RESOLUTION, uint32 numCascades = RendererVKLayout::NUM_SHADOW_CASCADES);

    vk::RenderPass getRenderPass() const          { return m_renderPass; }
    vk::Framebuffer getFramebuffer(uint32 i) const { return m_framebuffers[i]; }
    vk::ImageView getSampleView() const           { return m_sampleView; }
    vk::Sampler getSampler() const                { return m_sampler; }
    vk::Image getImage() const                    { return m_image; }
    uint32 getResolution() const                  { return m_resolution; }
    uint32 getNumCascades() const                 { return m_numCascades; }

private:

    void destroy();

    uint32 m_resolution = 0;
    uint32 m_numCascades = 0;
    vk::Image m_image;
    vk::DeviceMemory m_imageMemory;
    vk::ImageView m_sampleView; // e2DArray, all layers, for sampling in the lighting shader
    std::vector<vk::ImageView> m_layerViews; // one e2D view per cascade, used as a framebuffer attachment
    std::vector<vk::Framebuffer> m_framebuffers;
    vk::RenderPass m_renderPass;
    vk::Sampler m_sampler;
};
