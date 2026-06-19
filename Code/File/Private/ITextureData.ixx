export module File:ITextureData;

import File.fwd;

import Core;

export class ITextureData
{
public:
    virtual ~ITextureData() = default;

    struct Pixel
    {
        uint8 r, g, b, a;
    };

    virtual const char* getFileName() const = 0;
    virtual const Pixel* getPixels() const = 0;
	virtual uint32 getWidth() const = 0;
    virtual uint32 getHeight() const = 0; // if 0, pixels is a raw data buffer of size width
    virtual const char* getFormatInfo() const = 0;

    static std::unique_ptr<ITextureData> createFallbackDiffuseTexture();
    static std::unique_ptr<ITextureData> createFallbackWhiteTexture();
    static std::unique_ptr<ITextureData> createFallbackNormalTexture();
};