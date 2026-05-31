export module File.TextureData;

import Core;
import File.ITextureData;

export struct aiTexture;

export class TextureData final : public ITextureData
{
public:
	TextureData();
	~TextureData();
	TextureData(const TextureData&) = delete;
	TextureData(TextureData&&) = default;

	bool initialize(const aiTexture* pTexture);

	const char* getFileName() const override;
	const Pixel* getPixels() const override;
	uint32 getWidth() const override;
	uint32 getHeight() const override;
	const char* getFormatInfo() const override;

private:

	const char* m_pFileName = nullptr;
	const aiTexture* m_pTexture = nullptr;
};