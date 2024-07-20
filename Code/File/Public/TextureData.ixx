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
		char b, g, r, a;
	};

	const Pixel* getPixels();
	uint32 getWidth();
	uint32 getHeight();

private:

	const char* m_pFileName = nullptr;
	const aiTexture* m_pTexture = nullptr;
};