module RendererVK.Sampler;

import RendererVK.VK;
import RendererVK.Device;

Sampler::Sampler()
{
}

Sampler::~Sampler()
{
	if (m_sampler)
		m_device.destroySampler(m_sampler);
}

bool Sampler::initialize(const Device& device)
{
	m_device = device.getDevice();
	vk::SamplerCreateInfo samplerInfo = {
		.magFilter = vk::Filter::eNearest,
		.minFilter = vk::Filter::eLinear,
		.mipmapMode = vk::SamplerMipmapMode::eNearest,
		.addressModeU = vk::SamplerAddressMode::eRepeat,
		.addressModeV = vk::SamplerAddressMode::eRepeat,
		.addressModeW = vk::SamplerAddressMode::eRepeat,
		.mipLodBias = 0.0f,
		.anisotropyEnable = vk::True,
		.maxAnisotropy = 16,
		.compareEnable = vk::False,
		.compareOp = vk::CompareOp::eNever,
		.minLod = 0.0f,
		.maxLod = 0.0f,
		.borderColor = vk::BorderColor::eFloatOpaqueBlack,
		.unnormalizedCoordinates = vk::False
	};
	m_sampler = m_device.createSampler(samplerInfo);
	if (!m_sampler)
	{
		assert(false && "Failed to create sampler");
		return false;
	}

	return true;
}