export module RendererVK:Texture;

import Core;
import File.fwd;

import :VK;
import :Device;

export class StagingManager;

export class Texture final
{
public:
    Texture();
    ~Texture();
    Texture(const Texture& copy) = delete;
    Texture(Texture&& move);

    bool initialize(const char* filePath, bool generateMips);
    // sRGB: the pixel data is sRGB-encoded color (albedo/emissive) — sample through an Srgb format so
    // the hardware linearizes it. Data textures (normals, metal/roughness) must stay Unorm.
    bool initialize(const ITextureData& textureData, bool generateMips, bool sRGB = false);
    bool initialize(uint32 width, uint32 height, vk::Format format, const std::vector<std::span<uint8>>& imageDataMips, bool generateMips);

    vk::ImageView getImageView() const { return m_imageView; }
    vk::Image getImage() const { return m_image; }

private:

    uint32 m_width = 0;
    uint32 m_height = 0;
    uint32 m_numMipLevels = 0;
    vk::Format m_format;
    vk::Image m_image;
    vk::DeviceMemory m_imageMemory;
    vk::ImageView m_imageView;
};