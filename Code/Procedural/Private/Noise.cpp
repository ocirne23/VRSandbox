module Procedural;

import Core;
import Core.glm;
import :Noise;

namespace Procedural
{
	namespace
	{
		// Integer hash: map a lattice coordinate + seed to a well-mixed 32-bit value.
		uint32 hashCoord(int32 ix, int32 iy, uint32 seed)
		{
			uint32 h = seed + 0x9e3779b9u;
			h ^= (uint32)ix * 0x8da6b343u;
			h ^= (uint32)iy * 0xd8163841u;
			h *= 0x27d4eb2fu;
			h ^= h >> 15;
			h *= 0x85ebca6bu;
			h ^= h >> 13;
			return h;
		}
	}

	glm::vec2 NoiseField::gradient(int32 ix, int32 iy) const
	{
		const uint32 h = hashCoord(ix, iy, m_seed);
		const float angle = (float)(h & 0xffffu) / 65535.0f * 6.28318530718f;
		return glm::vec2(std::cos(angle), std::sin(angle));
	}

	float NoiseField::noise2D(float x, float y) const
	{
		const int32 ix = (int32)std::floor(x);
		const int32 iy = (int32)std::floor(y);
		const float fx = x - (float)ix;
		const float fy = y - (float)iy;

		// Quintic smoothstep fade
		const float u = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
		const float v = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);

		auto corner = [&](int32 cx, int32 cy) -> float
		{
			const glm::vec2 g = gradient(cx, cy);
			return g.x * (x - (float)cx) + g.y * (y - (float)cy);
		};

		const float n00 = corner(ix,     iy);
		const float n10 = corner(ix + 1, iy);
		const float n01 = corner(ix,     iy + 1);
		const float n11 = corner(ix + 1, iy + 1);

		const float nx0 = glm::mix(n00, n10, u);
		const float nx1 = glm::mix(n01, n11, u);
		// Gradient noise output is ~[-0.707, 0.707]; scale to ~[-1, 1].
		return glm::mix(nx0, nx1, v) * 1.41421356f;
	}

	float NoiseField::fbm(float x, float y, uint32 octaves, float lacunarity, float persistence) const
	{
		float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, norm = 0.0f;
		for (uint32 i = 0; i < octaves; ++i)
		{
			sum += noise2D(x * frequency, y * frequency) * amplitude;
			norm += amplitude;
			amplitude *= persistence;
			frequency *= lacunarity;
		}
		return norm > 0.0f ? sum / norm : 0.0f;
	}

	float NoiseField::ridged(float x, float y, uint32 octaves, float lacunarity, float persistence) const
	{
		float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, norm = 0.0f;
		for (uint32 i = 0; i < octaves; ++i)
		{
			float n = 1.0f - std::fabs(noise2D(x * frequency, y * frequency));
			n *= n; // sharpen the ridges
			sum += n * amplitude;
			norm += amplitude;
			amplitude *= persistence;
			frequency *= lacunarity;
		}
		return norm > 0.0f ? sum / norm : 0.0f;
	}

	float NoiseField::billow(float x, float y, uint32 octaves, float lacunarity, float persistence) const
	{
		float sum = 0.0f, amplitude = 1.0f, frequency = 1.0f, norm = 0.0f;
		for (uint32 i = 0; i < octaves; ++i)
		{
			sum += std::fabs(noise2D(x * frequency, y * frequency)) * amplitude;
			norm += amplitude;
			amplitude *= persistence;
			frequency *= lacunarity;
		}
		return norm > 0.0f ? sum / norm : 0.0f;
	}
}
