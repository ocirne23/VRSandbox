export module File.TextureData;

import Core;

export struct aiTexture;

export class TextureData final
{
public:
    TextureData();
    ~TextureData();
    TextureData(const TextureData&) = delete;
    TextureData(TextureData&&) = default;

    bool initialize(const aiTexture* pTexture);

    struct Pixel
    {
        char a, r, g, b;
    };

    const Pixel* getPixels() const;
    uint32 getWidth() const;
    uint32 getHeight() const;
    const char* getFormatInfo() const;

private:

    const char* m_pFileName = nullptr;
    const aiTexture* m_pTexture = nullptr;
};