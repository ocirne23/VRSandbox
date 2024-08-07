module RendererVK.Sampler;

import RendererVK.VK;
import RendererVK.Device;

Sampler::Sampler()
{
}

Sampler::~Sampler()
{
    if (m_sampler)
        VK::g_dev.getDevice().destroySampler(m_sampler);
}

bool Sampler::initialize()
{
    vk::SamplerCreateInfo samplerInfo = {
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eRepeat,
        .addressModeV = vk::SamplerAddressMode::eRepeat,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        .mipLodBias = 0.0f,
        .anisotropyEnable = vk::True,
        .maxAnisotropy = 16,
        .compareEnable = vk::False,
        .compareOp = vk::CompareOp::eNever,
        .minLod = 0.0f,
        .maxLod = vk::LodClampNone,
        .borderColor = vk::BorderColor::eFloatOpaqueBlack,
        .unnormalizedCoordinates = vk::False
    };
    m_sampler = VK::g_dev.getDevice().createSampler(samplerInfo);
    if (!m_sampler)
    {
        assert(false && "Failed to create sampler");
        return false;
    }

    return true;
}