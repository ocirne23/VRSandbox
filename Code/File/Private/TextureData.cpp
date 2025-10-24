module File.TextureData;

import File.Assimp;

using namespace Assimp;

TextureData::TextureData()
{
}

TextureData::~TextureData()
{
}

bool TextureData::initialize(const aiTexture* pTexture)
{
    m_pTexture = pTexture;
    m_pFileName = m_pTexture->mFilename.C_Str();
    return true;
}

const TextureData::Pixel* TextureData::getPixels() const
{
    return reinterpret_cast<const Pixel*>(m_pTexture->pcData);
}

uint32 TextureData::getWidth() const
{
    return m_pTexture->mWidth;
}

uint32 TextureData::getHeight() const
{
    return m_pTexture->mHeight;
}

const char* TextureData::getFormatInfo() const
{
    return m_pTexture->achFormatHint;
}