export module RendererVK:TextureManager;

import Core;
import File.fwd;

import :Layout;
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

    // Live texture-array descriptor capacity; grown by upload() (doubling, clamped to getDescriptorCap()).
    // Descriptor sets are allocated with this as their variable descriptor count, so the Renderer watches
    // getGeneration() and re-allocates them when it changes (the pipelines/layouts stay untouched).
    uint32 getMaxTextures() const { return m_maxTextures; }
    uint32 getGeneration() const { return m_generation; }
    // Fixed upper bound the descriptor set layouts declare: the device's per-stage sampled image limit
    // (with margin for the non-array image bindings), clamped to the uint16 index space and a soft cap
    // matched to the descriptor pool sizing in Device.cpp.
    uint32 getDescriptorCap() const;

private:

    std::vector<Texture> m_textures;
    uint32 m_maxTextures = RendererVKLayout::INITIAL_TEXTURES;
    uint32 m_generation = 0;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU4")
    TextureManager textureManager;
#pragma warning(default: 4075)
}