module RendererVK:ShadowMap;

import :VK;
import :Device;

constexpr vk::Format SHADOW_DEPTH_FORMAT = vk::Format::eD32Sfloat;

ShadowMap::ShadowMap() {}
ShadowMap::~ShadowMap()
{
    destroy();
}

void ShadowMap::destroy()
{
    vk::Device vkDevice = Globals::device.getDevice();
    if (m_sampler)      vkDevice.destroySampler(m_sampler);
    for (vk::Framebuffer fb : m_framebuffers) vkDevice.destroyFramebuffer(fb);
    m_framebuffers.clear();
    if (m_renderPass)   vkDevice.destroyRenderPass(m_renderPass);
    for (vk::ImageView v : m_layerViews) vkDevice.destroyImageView(v);
    m_layerViews.clear();
    if (m_sampleView)   vkDevice.destroyImageView(m_sampleView);
    if (m_image)        vkDevice.destroyImage(m_image);
    if (m_imageMemory)  vkDevice.freeMemory(m_imageMemory);
    m_sampler = nullptr; m_renderPass = nullptr; m_sampleView = nullptr; m_image = nullptr; m_imageMemory = nullptr;
}

bool ShadowMap::initialize(uint32 resolution, uint32 numCascades)
{
    vk::Device vkDevice = Globals::device.getDevice();
    m_resolution = resolution;
    m_numCascades = numCascades;

    // ---- Depth array image -------------------------------------------------
    vk::ImageCreateInfo imageInfo{
        .imageType = vk::ImageType::e2D,
        .format = SHADOW_DEPTH_FORMAT,
        .extent = { resolution, resolution, 1 },
        .mipLevels = 1,
        .arrayLayers = numCascades,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };
    auto imageResult = vkDevice.createImage(imageInfo);
    if (imageResult.result != vk::Result::eSuccess) { assert(false && "shadow image"); return false; }
    m_image = imageResult.value;

    vk::MemoryRequirements memReq = vkDevice.getImageMemoryRequirements(m_image);
    vk::MemoryAllocateInfo allocInfo{
        .allocationSize = memReq.size,
        .memoryTypeIndex = Globals::device.findMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
    };
    auto allocResult = vkDevice.allocateMemory(allocInfo);
    if (allocResult.result != vk::Result::eSuccess) { assert(false && "shadow mem"); return false; }
    m_imageMemory = allocResult.value;
    if (vkDevice.bindImageMemory(m_image, m_imageMemory, 0) != vk::Result::eSuccess) { assert(false && "shadow bind"); return false; }

    // ---- Views -------------------------------------------------------------
    vk::ImageViewCreateInfo sampleViewInfo{
        .image = m_image,
        .viewType = vk::ImageViewType::e2DArray,
        .format = SHADOW_DEPTH_FORMAT,
        .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = numCascades },
    };
    auto sampleViewResult = vkDevice.createImageView(sampleViewInfo);
    if (sampleViewResult.result != vk::Result::eSuccess) { assert(false && "shadow sample view"); return false; }
    m_sampleView = sampleViewResult.value;

    m_layerViews.reserve(numCascades);
    for (uint32 i = 0; i < numCascades; i++)
    {
        vk::ImageViewCreateInfo layerViewInfo{
            .image = m_image,
            .viewType = vk::ImageViewType::e2D,
            .format = SHADOW_DEPTH_FORMAT,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eDepth, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = i, .layerCount = 1 },
        };
        auto r = vkDevice.createImageView(layerViewInfo);
        if (r.result != vk::Result::eSuccess) { assert(false && "shadow layer view"); return false; }
        m_layerViews.push_back(r.value);
    }

    // ---- Depth-only render pass -------------------------------------------
    vk::AttachmentDescription2 depthAttachment{
        .format = SHADOW_DEPTH_FORMAT,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
    vk::AttachmentReference2 depthRef{ .attachment = 0, .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal };
    vk::SubpassDescription2 subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 0,
        .pDepthStencilAttachment = &depthRef,
    };
    std::array<vk::SubpassDependency2, 2> dependencies{
        // Wait for prior shader reads of this layer to finish before we overwrite the depth.
        vk::SubpassDependency2{
            .srcSubpass = vk::SubpassExternal,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eFragmentShader,
            .dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
            .srcAccessMask = vk::AccessFlagBits::eShaderRead,
            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        },
        // Make the written depth visible to the lighting fragment shader.
        vk::SubpassDependency2{
            .srcSubpass = 0,
            .dstSubpass = vk::SubpassExternal,
            .srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,
            .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
        },
    };
    vk::RenderPassCreateInfo2 renderPassInfo{
        .attachmentCount = 1,
        .pAttachments = &depthAttachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = (uint32)dependencies.size(),
        .pDependencies = dependencies.data(),
    };
    auto rpResult = vkDevice.createRenderPass2(renderPassInfo);
    if (rpResult.result != vk::Result::eSuccess) { assert(false && "shadow renderpass"); return false; }
    m_renderPass = rpResult.value;

    // ---- One framebuffer per cascade layer --------------------------------
    m_framebuffers.reserve(numCascades);
    for (uint32 i = 0; i < numCascades; i++)
    {
        vk::FramebufferCreateInfo fbInfo{
            .renderPass = m_renderPass,
            .attachmentCount = 1,
            .pAttachments = &m_layerViews[i],
            .width = resolution,
            .height = resolution,
            .layers = 1,
        };
        auto r = vkDevice.createFramebuffer(fbInfo);
        if (r.result != vk::Result::eSuccess) { assert(false && "shadow framebuffer"); return false; }
        m_framebuffers.push_back(r.value);
    }

    // ---- Comparison sampler (hardware PCF) --------------------------------
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToBorder,
        .addressModeV = vk::SamplerAddressMode::eClampToBorder,
        .addressModeW = vk::SamplerAddressMode::eClampToBorder,
        .anisotropyEnable = vk::False,
        .compareEnable = vk::True,
        .compareOp = vk::CompareOp::eLessOrEqual,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eFloatOpaqueWhite, // outside the map => fully lit
        .unnormalizedCoordinates = vk::False,
    };
    auto samplerResult = vkDevice.createSampler(samplerInfo);
    if (samplerResult.result != vk::Result::eSuccess) { assert(false && "shadow sampler"); return false; }
    m_sampler = samplerResult.value;

    return true;
}
