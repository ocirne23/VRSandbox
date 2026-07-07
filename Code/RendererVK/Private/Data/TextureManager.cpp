module RendererVK;

import File.fwd;
import :TextureManager; // ?
import :TextureStreamer;
import :Device;

TextureManager::~TextureManager()
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in TextureManager::~TextureManager");
    }
}

/*
uint16 TextureManager::upload(const std::vector<ITextureData*>& textureData, bool generateMips)
{
    if (m_textures.size() + textureData.size() > UINT16_MAX)
    {
        assert(false && "Too many textures");
        return UINT16_MAX;
    }
    const uint16 startIdx = (uint16)m_textures.size();
    const size_t numTextures = textureData.size();
    m_textures.reserve(startIdx + numTextures);
    for (uint32 i = 0; i < textureData.size(); ++i)
    {
        m_textures.emplace_back();
        if (!m_textures[startIdx + i].initialize(*textureData[i], generateMips))
        {
            assert(false && "Failed to initialize texture");
            m_textures.resize(startIdx);
            return UINT16_MAX;
        }
    }

    return startIdx;
}*/

uint32 TextureManager::getDescriptorCap() const
{
	const uint32 deviceLimit = Globals::device.getPhysicalDevice().getProperties().limits.maxPerStageDescriptorSampledImages;
	return std::min({ deviceLimit - 16u, (uint32)UINT16_MAX - 1u, 16u * 1024u });
}

uint16 TextureManager::upload(const ITextureData& textureData, bool generateMips, bool sRGB)
{
	assert(m_textures.size() < UINT16_MAX && "Too many textures");
	if ((uint32)m_textures.size() >= m_maxTextures)
	{
		const uint32 maxCapacity = getDescriptorCap();
		if (m_maxTextures < maxCapacity)
		{
			m_maxTextures = std::min(m_maxTextures * 2u, maxCapacity);
			m_generation++;
			printf("TextureManager: grew texture capacity to %u\n", m_maxTextures);
		}
		else
		{
			assert(false && "Texture capacity at device limit");
		}
	}
	const uint16 idx = (uint16)m_textures.size();
	m_textures.emplace_back();
	if (!m_textures[idx].initialize(textureData, generateMips, sRGB))
	{
		assert(false && "Failed to initialize texture");
		m_textures.pop_back();
		return UINT16_MAX;
	}
	const uint64 allocatedBytes = m_textures[idx].getAllocatedBytes();
	if (std::unique_ptr<StreamedTextureMeta> pMeta = m_textures[idx].takeStreamingMeta())
		Globals::textureStreamer.registerTexture(idx, std::move(*pMeta), allocatedBytes);
	else
		Globals::textureStreamer.notePinned(allocatedBytes);
	return idx;
}