export module RendererVK.Sampler;

import RendererVK.VK;

export class Sampler final
{
public:
    Sampler();
    ~Sampler();
    Sampler(const Sampler&) = delete;

    bool initialize();

    vk::Sampler getSampler() const { return m_sampler; }

private:

    vk::Sampler m_sampler;
};