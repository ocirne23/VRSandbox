export module RendererVK.TextureManager;

import Core;
import File.TextureData;
import RendererVK.Texture;

export class TextureManager final
{
public:

    uint16 upload(const std::vector<TextureData>& textureData);
    const Texture& getTexture(uint16 idx) const { assert(idx < m_textures.size()); return m_textures[idx]; }
    const std::vector<Texture>& getTextures() const { return m_textures; }

private:

    std::vector<Texture> m_textures;
};

export namespace Globals
{
    TextureManager textureManager;
}