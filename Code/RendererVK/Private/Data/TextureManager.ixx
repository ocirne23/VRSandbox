export module RendererVK.TextureManager;

import Core;
import File.TextureData;
import RendererVK.Texture;

export class TextureManager final
{
public:

    ~TextureManager();
    uint16 upload(const std::vector<TextureData>& textureData);
    const Texture& getTexture(uint16 idx) const { assert(idx < m_textures.size()); return m_textures[idx]; }
    const std::vector<Texture>& getTextures() const { return m_textures; }

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