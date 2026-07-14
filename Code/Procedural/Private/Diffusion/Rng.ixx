export module Procedural:Diffusion.Rng;

import Core;

// Portable RNG for the Terrain Diffusion pipeline (port of terrain_diffusion/inference/portable_rng.py via
// the Java mod's PortableRng/GaussianNoisePatch). PCG64 (64-bit LCG + XSH-RR 64/32) with standard normals via
// Marsaglia polar. Every value the world is built from starts here, so this must stay bit-identical to the
// reference: the algorithm IS the world seed, not just a source of randomness.
export namespace Procedural::Diffusion
{
	// Floor division / modulus (round toward negative infinity), matching Java's Math.floorDiv/floorMod.
	// C++ '/' truncates toward zero, which is WRONG for the negative world coordinates this pipeline is
	// full of. Every place the reference says Math.floorDiv must call this.
	int32 floorDiv(int32 a, int32 b);
	int32 floorMod(int32 a, int32 b);

	// Portable 64-bit tile seed from (baseSeed, ty, tx). Matches world_pipeline._tile_seed.
	uint64 tileSeed(uint64 baseSeed, int32 ty, int32 tx);

	// Fill [out, out+count) with standard normals (Marsaglia polar over PCG64).
	void fillStandardNormal(uint64 seed, float* out, size_t count);

	// A (channels, h, w) patch of standard-normal noise, seeded per (tileH x tileW) tile so that any window
	// of the infinite plane sees the same values regardless of how it was requested. Row-major, channel-major:
	// out[c*h*w + y*w + x]. (y0, x0) may be negative. `out` must hold channels*h*w floats.
	void gaussianNoisePatch(uint64 baseSeed, int32 y0, int32 x0, int32 h, int32 w,
	                        int32 channels, int32 tileH, int32 tileW, float* out);
}
