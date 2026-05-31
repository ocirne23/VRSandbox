module File.ProceduralTextureData;

import Core;

ProceduralTextureData::ProceduralTextureData()
{
}

ProceduralTextureData::~ProceduralTextureData()
{
}

bool ProceduralTextureData::initialize(EProceduralTextureType type, uint32 width, uint32 height)
{
	m_width  = width;
	m_height = height;
	m_pixels.resize(width * height);

	switch (type)
	{
	case EProceduralTextureType::Checkerboard:
	{
		m_fileName = "procedural_checkerboard";
		constexpr uint32 tileSize = 8;
		for (uint32 y = 0; y < height; y++)
		{
			for (uint32 x = 0; x < width; x++)
			{
				const bool isLight = ((x / tileSize) + (y / tileSize)) % 2 == 0;
				const char v = isLight ? (uint8)0xFF : (uint8)0x00;
				Pixel& p = m_pixels[y * width + x];
				p.r = v;
				p.g = v;
				p.b = v;
				p.a = (uint8)0xFF;
			}
		}
		break;
	}
	case EProceduralTextureType::White:
	{
		m_fileName = "procedural_white";
		for (Pixel& p : m_pixels)
		{
			p.a = (uint8)0xFF;
			p.r = (uint8)0xFF;
			p.g = (uint8)0xFF;
			p.b = (uint8)0xFF;
		}
		break;
	}
	case EProceduralTextureType::FlatNormal:
	{
		m_fileName = "procedural_flat_normal";
		// Flat normal map: (0.5, 0.5, 1.0) in RGB => (128, 128, 255)
		for (Pixel& p : m_pixels)
		{
			p.r = (uint8)0x80; // 128
			p.g = (uint8)0x80; // 128
			p.b = (uint8)0xFF; // 255
			p.a = (uint8)0xFF;
		}
		break;
	}
	default:
		return false;
	}

	return true;
}

const char*  ProceduralTextureData::getFileName()   const { return m_fileName.c_str(); }
const ProceduralTextureData::Pixel* ProceduralTextureData::getPixels() const { return m_pixels.data(); }
uint32       ProceduralTextureData::getWidth()      const { return m_width; }
uint32       ProceduralTextureData::getHeight()     const { return m_height; }
const char*  ProceduralTextureData::getFormatInfo() const { return "rgba8888"; }
