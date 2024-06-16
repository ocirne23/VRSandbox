module;

#include "VK.h"

export module RendererVK.Sampler;

import RendererVK.Device;

export class Sampler
{
public:
	Sampler();
	~Sampler();
	Sampler(const Sampler&) = delete;

	bool initialize(const Device& device);

	vk::Sampler getSampler() const { return m_sampler; }

private:

	vk::Sampler m_sampler;
	vk::Device m_device;
};