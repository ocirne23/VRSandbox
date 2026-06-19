module RendererVK:SceneColor;

import :VK;
import :Device;
import :Allocator;
import :CommandBuffer;

namespace
{
    constexpr vk::Format SCENE_DEPTH_FORMAT = vk::Format::eD32Sfloat;

    bool createImage(vk::Device vkDevice, uint32 w, uint32 h, vk::Format format, vk::ImageUsageFlags usage,
        uint32 arrayLayers, vk::Image& outImage, VmaAllocation& outMemory, const char* name)
    {
        vk::ImageCreateInfo info{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = { w, h, 1 },
            .mipLevels = 1,
            .arrayLayers = arrayLayers,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = usage,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        if (!Globals::gpuAllocator.createImage(info, outImage, outMemory, name)) { assert(false && "scenecolor image"); return false; }
        return true;
    }

    bool createView(vk::Device vkDevice, vk::Image image, vk::Format format, vk::ImageAspectFlags aspect,
        vk::ImageViewType viewType, uint32 baseLayer, uint32 layerCount, vk::ImageView& outView)
    {
        vk::ImageViewCreateInfo info{
            .image = image,
            .viewType = viewType,
            .format = format,
            .subresourceRange = { .aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = baseLayer, .layerCount = layerCount },
        };
        auto result = vkDevice.createImageView(info);
        if (result.result != vk::Result::eSuccess) { assert(false && "scenecolor view"); return false; }
        outView = result.value;
        return true;
    }
}

SceneColor::SceneColor() {}
SceneColor::~SceneColor() { destroy(); }

void SceneColor::destroy()
{
    vk::Device vkDevice = Globals::device.getDevice();
    if (m_sampler)      vkDevice.destroySampler(m_sampler);
    for (vk::Framebuffer& fb : m_framebuffers) { if (fb) vkDevice.destroyFramebuffer(fb); fb = nullptr; }
    if (m_renderPass)   vkDevice.destroyRenderPass(m_renderPass);
    for (vk::ImageView& v : m_colorLayerViews) { if (v) vkDevice.destroyImageView(v); v = nullptr; }
    for (vk::ImageView& v : m_depthLayerViews) { if (v) vkDevice.destroyImageView(v); v = nullptr; }
    Globals::gpuAllocator.destroyImage(m_colorImage, m_colorMemory);
    Globals::gpuAllocator.destroyImage(m_depthImage, m_depthMemory);
    m_sampler = nullptr; m_renderPass = nullptr;
    m_colorImage = nullptr; m_depthImage = nullptr;
    m_colorMemory = nullptr; m_depthMemory = nullptr;
}

bool SceneColor::initialize(vk::Format colorFormat, uint32 width, uint32 height, uint32 viewCount)
{
    vk::Device vkDevice = Globals::device.getDevice();
    destroy();
    m_width = width;
    m_height = height;
    m_viewCount = viewCount;
    m_colorFormat = colorFormat;

    // The colour/depth images hold one layer per eye; the forward pass renders into each layer in a
    // separate (non-multiview) pass. TRANSFER_SRC lets the VR path copy each layer into its eye swapchain.
    if (!createImage(vkDevice, width, height, colorFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
        viewCount, m_colorImage, m_colorMemory, "SceneColor.color")) return false;
    if (!createImage(vkDevice, width, height, SCENE_DEPTH_FORMAT,
        vk::ImageUsageFlagBits::eDepthStencilAttachment, viewCount, m_depthImage, m_depthMemory, "SceneColor.depth")) return false;

    for (uint32 i = 0; i < viewCount; ++i)
    {
        if (!createView(vkDevice, m_colorImage, colorFormat, vk::ImageAspectFlagBits::eColor, vk::ImageViewType::e2D, i, 1, m_colorLayerViews[i])) return false;
        if (!createView(vkDevice, m_depthImage, SCENE_DEPTH_FORMAT, vk::ImageAspectFlagBits::eDepth, vk::ImageViewType::e2D, i, 1, m_depthLayerViews[i])) return false;
    }

    // ---- Render pass: colour (ends SHADER_READ_ONLY for the TAA compute pass) + transient depth ----
    std::array<vk::AttachmentDescription2, 2> attachments{
        vk::AttachmentDescription2{ // 0: scene colour
            .format = colorFormat,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        },
        vk::AttachmentDescription2{ // 1: depth (not sampled)
            .format = SCENE_DEPTH_FORMAT,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        },
    };
    vk::AttachmentReference2 colorRef{ .attachment = 0, .layout = vk::ImageLayout::eColorAttachmentOptimal, .aspectMask = vk::ImageAspectFlagBits::eColor };
    vk::AttachmentReference2 depthRef{ .attachment = 1, .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal, .aspectMask = vk::ImageAspectFlagBits::eDepth };
    vk::SubpassDescription2 subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        // Non-multiview: each eye is a separate single-layer pass so the forward pass's DGC execution
        // set is allowed (an execution set requires viewMask == 0).
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorRef,
        .pDepthStencilAttachment = &depthRef,
    };
    // These MUST match the swapchain pass's dependencies (RenderPass.cpp): the static-mesh and GI-debug
    // pipelines are created against that pass but execute here, and render-pass compatibility (as enforced by
    // validation) compares subpass dependencies. The colour -> TAA-compute ordering is instead handled by an
    // explicit barrier after the scene render pass in Renderer::recordCommandBuffers (the finalLayout above
    // still transitions the colour image to SHADER_READ_ONLY).
    const vk::PipelineStageFlags depthStages = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
    std::array<vk::SubpassDependency2, 2> dependencies{
        vk::SubpassDependency2{
            .srcSubpass = vk::SubpassExternal,
            .dstSubpass = 0,
            .srcStageMask = depthStages,
            .dstStageMask = depthStages,
            .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        },
        vk::SubpassDependency2{
            .srcSubpass = vk::SubpassExternal,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .srcAccessMask = vk::AccessFlags(),
            .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        },
    };
    vk::RenderPassCreateInfo2 rpInfo{
        .attachmentCount = (uint32)attachments.size(),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = (uint32)dependencies.size(),
        .pDependencies = dependencies.data(),
    };
    auto rpResult = vkDevice.createRenderPass2(rpInfo);
    if (rpResult.result != vk::Result::eSuccess) { assert(false && "scenecolor renderpass"); return false; }
    m_renderPass = rpResult.value;

    for (uint32 i = 0; i < viewCount; ++i)
    {
        std::array<vk::ImageView, 2> fbViews{ m_colorLayerViews[i], m_depthLayerViews[i] };
        vk::FramebufferCreateInfo fbInfo{
            .renderPass = m_renderPass,
            .attachmentCount = (uint32)fbViews.size(),
            .pAttachments = fbViews.data(),
            .width = width,
            .height = height,
            .layers = 1,
        };
        auto fbResult = vkDevice.createFramebuffer(fbInfo);
        if (fbResult.result != vk::Result::eSuccess) { assert(false && "scenecolor framebuffer"); return false; }
        m_framebuffers[i] = fbResult.value;
    }

    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::False,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eFloatOpaqueBlack,
        .unnormalizedCoordinates = vk::False,
    };
    auto samplerResult = vkDevice.createSampler(samplerInfo);
    if (samplerResult.result != vk::Result::eSuccess) { assert(false && "scenecolor sampler"); return false; }
    m_sampler = samplerResult.value;

    // One-time layout init to SHADER_READ_ONLY so a never-yet-rendered target (e.g. the other frame's image
    // sampled before it has been drawn) is in a legal sampling layout. The render pass uses loadOp=Clear with
    // initialLayout=Undefined, so it does not depend on this.
    {
        CommandBuffer init;
        init.initialize(vk::CommandBufferLevel::ePrimary);
        vk::CommandBuffer cmd = init.begin(true);
        vk::ImageMemoryBarrier2 bar{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = m_colorImage,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, viewCount },
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &bar });
        init.end();
        init.submitGraphics();
        (void)Globals::device.getGraphicsQueue().waitIdle();
    }

    return true;
}
