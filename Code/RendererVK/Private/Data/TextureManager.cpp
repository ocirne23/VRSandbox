module RendererVK:TextureManager;

import File.fwd;

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

uint16 TextureManager::upload(const ITextureData& textureData, bool generateMips)
{
	assert(m_textures.size() < UINT16_MAX && "Too many textures");
	const uint16 idx = (uint16)m_textures.size();
	m_textures.emplace_back();
	if (!m_textures[idx].initialize(textureData, generateMips))
	{
		assert(false && "Failed to initialize texture");
		m_textures.pop_back();
		return UINT16_MAX;
	}
	return idx;
}