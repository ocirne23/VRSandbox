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
    memcpy(m_formatHint, pTexture->achFormatHint, sizeof(m_formatHint));
    m_pFileName = pTexture->mFilename.C_Str();
	m_width = pTexture->mWidth;
	m_height = pTexture->mHeight;
    m_pPixels = reinterpret_cast<const Pixel*>(pTexture->pcData);
    return true;
}