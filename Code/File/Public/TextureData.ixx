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

	const Pixel* getPixels() const { return m_pPixels; }
	uint32 getWidth() const { return m_width; }
    uint32 getHeight() const { return m_height; } // if 0, pixels is a raw data buffer of size width
    const char* getFormatInfo() const { return m_formatHint; }

private:

    const char* m_pFileName = nullptr;
	char m_formatHint[9] = {};
	uint32 m_width = 0;
	uint32 m_height = 0;
	const Pixel* m_pPixels = nullptr;
};