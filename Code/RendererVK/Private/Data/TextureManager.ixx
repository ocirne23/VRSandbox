export module RendererVK:TextureManager;

import Core;
import File.fwd;

import :Texture;

export class TextureManager final
{
public:

    ~TextureManager();
    //uint16 upload(const std::vector<ITextureData*>& textureData, bool generateMips);
    uint16 upload(const ITextureData& textureData, bool generateMips);
    const Texture& getTexture(uint16 idx) const { assert(idx < m_textures.size()); return m_textures[idx]; }
    const std::vector<Texture>& getTextures() const { return m_textures; }
	size_t getNumTextures() const { return m_textures.size(); }

private:

    std::vector<Texture> m_textures;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU4")
    TextureManager textureManager;
#pragma warning(default: 4075)
}