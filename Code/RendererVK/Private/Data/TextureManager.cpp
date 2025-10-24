module RendererVK.TextureManager;

uint16 TextureManager::upload(const std::vector<TextureData>& textureData)
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
        if (!m_textures[startIdx + i].initialize(textureData[i]))
        {
            assert(false && "Failed to initialize texture");
            m_textures.resize(startIdx);
            return UINT16_MAX;
        }
    }

    return startIdx;
}