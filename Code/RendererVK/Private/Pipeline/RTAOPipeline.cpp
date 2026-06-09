module RendererVK:RTAOPipeline;

import Core;
import Core.Tweaks;
import File.FileSystem;
import :Device;
import :CommandBuffer;

namespace
{
    // rgb = bent normal (world space, the average unoccluded direction), a = scalar AO. The forward pass
    // evaluates the GI probe SH along the bent normal for directional indirect occlusion.
    constexpr vk::Format AO_FORMAT = vk::Format::eR16G16B16A16Sfloat;

    struct RtaoPC
    {
        uint32 frameIndex;
        uint32 numRays;
        float  radius;
        float  power;
        float  intensity;
        uint32 aoWidth;
        uint32 aoHeight;
        float  _pad;
    };
    struct TemporalPC
    {
        uint32 aoWidth;
        uint32 aoHeight;
        float  maxHistory;
        float  _pad;
    };
    struct SpatialPC
    {
        uint32 aoWidth;
        uint32 aoHeight;
        int32  radius;
        float  _pad;
    };

    auto imgInfoGeneral(vk::ImageView view) { return vk::DescriptorImageInfo{ .imageView = view, .imageLayout = vk::ImageLayout::eGeneral }; }
    auto sampledGeneral(vk::Sampler s, vk::ImageView v) { return vk::DescriptorImageInfo{ .sampler = s, .imageView = v, .imageLayout = vk::ImageLayout::eGeneral }; }
    auto sampledRO(vk::Sampler s, vk::ImageView v) { return vk::DescriptorImageInfo{ .sampler = s, .imageView = v, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal }; }
}

void RTAOPipeline::buildTraceLayout(ComputePipelineLayout& layout)
{
    layout.computeShaderDebugFilePath = "Shaders/rtao.cs.glsl";
    layout.computeShaderText = FileSystem::readFileStr(layout.computeShaderDebugFilePath);
    auto& b = layout.descriptorSetLayoutBindings;
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 2, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 3, .descriptorType = vk::DescriptorType::eAccelerationStructureKHR, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 4, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(RtaoPC) });
}

void RTAOPipeline::buildTemporalLayout(ComputePipelineLayout& layout)
{
    layout.computeShaderDebugFilePath = "Shaders/ao_temporal.cs.glsl";
    layout.computeShaderText = FileSystem::readFileStr(layout.computeShaderDebugFilePath);
    auto& b = layout.descriptorSetLayoutBindings;
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    for (uint32 i = 1; i <= 4; ++i)
        b.push_back(vk::DescriptorSetLayoutBinding{ .binding = i, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 5, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(TemporalPC) });
}

void RTAOPipeline::buildSpatialLayout(ComputePipelineLayout& layout)
{
    layout.computeShaderDebugFilePath = "Shaders/ao_spatial.cs.glsl";
    layout.computeShaderText = FileSystem::readFileStr(layout.computeShaderDebugFilePath);
    auto& b = layout.descriptorSetLayoutBindings;
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    for (uint32 i = 1; i <= 3; ++i)
        b.push_back(vk::DescriptorSetLayoutBinding{ .binding = i, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    b.push_back(vk::DescriptorSetLayoutBinding{ .binding = 4, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute });
    layout.pushConstantRanges.push_back(vk::PushConstantRange{ .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(SpatialPC) });
}

void RTAOPipeline::destroyImageSet(ImageSet& set)
{
    vk::Device vkDevice = Globals::device.getDevice();
    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
    {
        if (set.view[i])   vkDevice.destroyImageView(set.view[i]);
        if (set.image[i])  vkDevice.destroyImage(set.image[i]);
        if (set.memory[i]) vkDevice.freeMemory(set.memory[i]);
        set.view[i] = nullptr; set.image[i] = nullptr; set.memory[i] = nullptr;
        set.initialized[i] = false;
    }
}

void RTAOPipeline::createImageSet(ImageSet& set)
{
    vk::Device vkDevice = Globals::device.getDevice();
    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
    {
        vk::ImageCreateInfo info{
            .imageType = vk::ImageType::e2D,
            .format = AO_FORMAT,
            .extent = { m_width, m_height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        auto imageResult = vkDevice.createImage(info);
        assert(imageResult.result == vk::Result::eSuccess);
        set.image[i] = imageResult.value;

        vk::MemoryRequirements memReq = vkDevice.getImageMemoryRequirements(set.image[i]);
        vk::MemoryAllocateInfo allocInfo{ .allocationSize = memReq.size,
            .memoryTypeIndex = Globals::device.findMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) };
        auto allocResult = vkDevice.allocateMemory(allocInfo);
        assert(allocResult.result == vk::Result::eSuccess);
        set.memory[i] = allocResult.value;
        (void)vkDevice.bindImageMemory(set.image[i], set.memory[i], 0);

        vk::ImageViewCreateInfo viewInfo{
            .image = set.image[i],
            .viewType = vk::ImageViewType::e2D,
            .format = AO_FORMAT,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1 },
        };
        auto viewResult = vkDevice.createImageView(viewInfo);
        assert(viewResult.result == vk::Result::eSuccess);
        set.view[i] = viewResult.value;
    }
}

void RTAOPipeline::recreateImages(uint32 fullWidth, uint32 fullHeight)
{
    destroyImageSet(m_raw);
    destroyImageSet(m_accum);
    destroyImageSet(m_final);
    m_width = (fullWidth + 1) / 2;
    m_height = (fullHeight + 1) / 2;
    createImageSet(m_raw);
    createImageSet(m_accum);
    createImageSet(m_final);

    // One-time UNDEFINED -> GENERAL for every AO image, so a never-yet-written image (e.g. the history
    // accum buffer sampled on the very first frame) is in the layout its descriptor was written with.
    ImageSet* sets[] = { &m_raw, &m_accum, &m_final };
    std::vector<vk::ImageMemoryBarrier2> bars;
    for (ImageSet* s : sets)
        for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
        {
            bars.push_back(vk::ImageMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .image = s->image[i],
                .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
            });
            s->initialized[i] = true;
        }
    CommandBuffer init;
    init.initialize(vk::CommandBufferLevel::ePrimary);
    vk::CommandBuffer cmd = init.begin(true);
    cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = (uint32)bars.size(), .pImageMemoryBarriers = bars.data() });
    init.end();
    init.submitGraphics();
    (void)Globals::device.getGraphicsQueue().waitIdle();
}

void RTAOPipeline::initialize(uint32 fullWidth, uint32 fullHeight)
{
    ComputePipelineLayout traceLayout;    buildTraceLayout(traceLayout);       m_tracePipeline.initialize(traceLayout);
    ComputePipelineLayout temporalLayout; buildTemporalLayout(temporalLayout); m_temporalPipeline.initialize(temporalLayout);
    ComputePipelineLayout spatialLayout;  buildSpatialLayout(spatialLayout);   m_spatialPipeline.initialize(spatialLayout);
    for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; ++i)
    {
        m_traceSets[i].initialize(m_tracePipeline.getDescriptorSetLayout());
        m_temporalSets[i].initialize(m_temporalPipeline.getDescriptorSetLayout());
        m_spatialSets[i].initialize(m_spatialPipeline.getDescriptorSetLayout());
    }

    recreateImages(fullWidth, fullHeight);

    vk::SamplerCreateInfo samplerInfo{
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
        .anisotropyEnable = vk::False,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = vk::BorderColor::eFloatOpaqueWhite,
        .unnormalizedCoordinates = vk::False,
    };
    auto samplerResult = Globals::device.getDevice().createSampler(samplerInfo);
    assert(samplerResult.result == vk::Result::eSuccess);
    m_aoSampler = samplerResult.value;

    Tweak::intVar("RTAO", "Rays Per Pixel", &m_rays, 1, 32);
    Tweak::floatVar("RTAO", "Radius", &m_radius, 0.0f, 32.0f);
    Tweak::floatVar("RTAO", "Power", &m_power, 0.0f, 8.0f);
    Tweak::floatVar("RTAO", "Intensity", &m_intensity, 0.0f, 4.0f);
    Tweak::floatVar("RTAO", "Max History", &m_maxHistory, 0.0f, 1.0f);
    Tweak::intVar("RTAO", "Blur Radius", &m_blurRadius, 0, 8);
}

void RTAOPipeline::reloadShaders()
{
    ComputePipelineLayout traceLayout;    buildTraceLayout(traceLayout);
    ComputePipelineLayout temporalLayout; buildTemporalLayout(temporalLayout);
    ComputePipelineLayout spatialLayout;  buildSpatialLayout(spatialLayout);
    bool ok = m_tracePipeline.reloadShaders(traceLayout);
    ok = m_temporalPipeline.reloadShaders(temporalLayout) && ok;
    ok = m_spatialPipeline.reloadShaders(spatialLayout) && ok;
    if (!ok)
        printf("RTAOPipeline: shader reload failed, keeping previous pipeline(s)\n");
}

void RTAOPipeline::transitionToGeneral(vk::CommandBuffer cmd, ImageSet& set, uint32 frameIdx, bool forWrite)
{
    vk::ImageMemoryBarrier2 bar{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = forWrite ? vk::AccessFlagBits2::eShaderStorageWrite : vk::AccessFlagBits2::eShaderSampledRead,
        .oldLayout = set.initialized[frameIdx] ? vk::ImageLayout::eGeneral : vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eGeneral,
        .image = set.image[frameIdx],
        .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
    };
    cmd.pipelineBarrier2(vk::DependencyInfo{ .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &bar });
    set.initialized[frameIdx] = true;
}

void RTAOPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, const RecordParams& params)
{
    vk::CommandBuffer cmd = commandBuffer.getCommandBuffer();
    const uint32 prevIdx = (frameIdx + 1) % RendererVKLayout::NUM_FRAMES_IN_FLIGHT;
    vk::PipelineLayout traceLayout = m_tracePipeline.getPipelineLayout();
    vk::PipelineLayout temporalLayout = m_temporalPipeline.getPipelineLayout();
    vk::PipelineLayout spatialLayout = m_spatialPipeline.getPipelineLayout();
    const uint32 gx = (m_width + 7) / 8;
    const uint32 gy = (m_height + 7) / 8;

    auto storeWriteToSampledRead = [&]() {
        vk::MemoryBarrier2 bar{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
    };

    auto uboInfo = vk::DescriptorBufferInfo{ .buffer = params.ubo.getBuffer(), .range = sizeof(RendererVKLayout::Ubo) };

    // -------- Pass 1: trace raw AO --------
    {
        vk::MemoryBarrier2 asVis{
            .srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
        };
        cmd.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &asVis });
        transitionToGeneral(cmd, m_raw, frameIdx, true);

        DescriptorSet& set = m_traceSets[frameIdx];
        vk::DescriptorSet vkSet = set.getDescriptorSet();
        std::array<DescriptorSetUpdateInfo, 4> updates{
            DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer, .bufferInfos = { uboInfo } },
            DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledRO(params.gbufferSampler, params.gbufferNormalView) } },
            DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledRO(params.gbufferSampler, params.gbufferDepthView) } },
            DescriptorSetUpdateInfo{ .binding = 4, .type = vk::DescriptorType::eStorageImage, .imageInfos = { imgInfoGeneral(m_raw.view[frameIdx]) } },
        };
        commandBuffer.cmdUpdateDescriptorSets(traceLayout, vk::PipelineBindPoint::eCompute, vkSet, updates);
        vk::WriteDescriptorSetAccelerationStructureKHR asInfo{ .accelerationStructureCount = 1, .pAccelerationStructures = &params.tlas };
        vk::WriteDescriptorSet asWrite{ .pNext = &asInfo, .dstSet = vkSet, .dstBinding = 3, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eAccelerationStructureKHR };
        Globals::device.getDevice().updateDescriptorSets(1, &asWrite, 0, nullptr);

        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_tracePipeline.getPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, traceLayout, 0, 1, &vkSet, 0, nullptr);
        RtaoPC pc{ .frameIndex = params.frameIndex, .numRays = uint32(std::max(m_rays, 1)), .radius = m_radius, .power = m_power,
            .intensity = m_intensity, .aoWidth = m_width, .aoHeight = m_height };
        cmd.pushConstants(traceLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
        cmd.dispatch(gx, gy, 1);
    }
    storeWriteToSampledRead();

    // -------- Pass 2: temporal accumulate (read raw + history accum[prev], write accum[cur]) --------
    {
        transitionToGeneral(cmd, m_accum, frameIdx, true);

        DescriptorSet& set = m_temporalSets[frameIdx];
        vk::DescriptorSet vkSet = set.getDescriptorSet();
        std::array<DescriptorSetUpdateInfo, 6> updates{
            DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer, .bufferInfos = { uboInfo } },
            DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledRO(params.gbufferSampler, params.gbufferDepthView) } },
            DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledRO(params.gbufferSampler, params.prevGbufferDepthView) } },
            DescriptorSetUpdateInfo{ .binding = 3, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledGeneral(m_aoSampler, m_raw.view[frameIdx]) } },
            DescriptorSetUpdateInfo{ .binding = 4, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledGeneral(m_aoSampler, m_accum.view[prevIdx]) } },
            DescriptorSetUpdateInfo{ .binding = 5, .type = vk::DescriptorType::eStorageImage, .imageInfos = { imgInfoGeneral(m_accum.view[frameIdx]) } },
        };
        commandBuffer.cmdUpdateDescriptorSets(temporalLayout, vk::PipelineBindPoint::eCompute, vkSet, updates);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_temporalPipeline.getPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, temporalLayout, 0, 1, &vkSet, 0, nullptr);
        TemporalPC pc{ .aoWidth = m_width, .aoHeight = m_height, .maxHistory = m_maxHistory };
        cmd.pushConstants(temporalLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
        cmd.dispatch(gx, gy, 1);
    }
    storeWriteToSampledRead();

    // -------- Pass 3: spatial bilateral blur (read accum[cur], write final[cur]) --------
    {
        transitionToGeneral(cmd, m_final, frameIdx, true);

        DescriptorSet& set = m_spatialSets[frameIdx];
        vk::DescriptorSet vkSet = set.getDescriptorSet();
        std::array<DescriptorSetUpdateInfo, 5> updates{
            DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer, .bufferInfos = { uboInfo } },
            DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledRO(params.gbufferSampler, params.gbufferDepthView) } },
            DescriptorSetUpdateInfo{ .binding = 2, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledRO(params.gbufferSampler, params.gbufferNormalView) } },
            DescriptorSetUpdateInfo{ .binding = 3, .type = vk::DescriptorType::eCombinedImageSampler, .imageInfos = { sampledGeneral(m_aoSampler, m_accum.view[frameIdx]) } },
            DescriptorSetUpdateInfo{ .binding = 4, .type = vk::DescriptorType::eStorageImage, .imageInfos = { imgInfoGeneral(m_final.view[frameIdx]) } },
        };
        commandBuffer.cmdUpdateDescriptorSets(spatialLayout, vk::PipelineBindPoint::eCompute, vkSet, updates);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, m_spatialPipeline.getPipeline());
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, spatialLayout, 0, 1, &vkSet, 0, nullptr);
        SpatialPC pc{ .aoWidth = m_width, .aoHeight = m_height, .radius = m_blurRadius };
        cmd.pushConstants(spatialLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
        cmd.dispatch(gx, gy, 1);
    }
    storeWriteToSampledRead();
}
