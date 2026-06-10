export module File.ProceduralTextureData;

import Core;
import File.ITextureData;

export enum class EProceduralTextureType
{
	Checkerboard, // 8x8 black-and-white checker pattern
	White,        // solid white (1,1,1,1)
	FlatNormal,   // flat normal map (0.5, 0.5, 1.0, 1.0)
	SkyGradient,  // vertical zenith -> horizon -> ground gradient (for the sky sphere; v=0 = zenith)
};

export class ProceduralTextureData final : public ITextureData
{
public:
	ProceduralTextureData();
	~ProceduralTextureData();
	ProceduralTextureData(const ProceduralTextureData&) = delete;
	ProceduralTextureData(ProceduralTextureData&&) = default;

	bool initialize(EProceduralTextureType type, uint32 width = 64, uint32 height = 64);

	const char*  getFileName()  const override;
	const Pixel* getPixels()    const override;
	uint32       getWidth()     const override;
	uint32       getHeight()    const override;
	const char*  getFormatInfo() const override;

private:
	std::string        m_fileName;
	std::vector<Pixel> m_pixels;
	uint32             m_width  = 0;
	uint32             m_height = 0;
};
