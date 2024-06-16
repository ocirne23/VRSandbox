module;

#include <stdint.h>

export module File.TextureData;

export struct aiTexture;

export class TextureData
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
	uint32_t getWidth();
	uint32_t getHeight();

private:

	const char* m_pFileName = nullptr;
	const aiTexture* m_pTexture = nullptr;
};