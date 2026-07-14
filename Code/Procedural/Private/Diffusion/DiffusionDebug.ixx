export module Procedural:Diffusion.Debug;

import Core;

// Bring-up / debug hooks for the diffusion terrain stack.
//
// This is the only Diffusion partition re-exported from Public/Procedural.ixx, and it deliberately exposes
// PODs only — no ONNX, FastNoiseLite or tensor types cross the boundary, so the runtime stays private to
// the library (same discipline as box3d in Physics).
export namespace Procedural::Diffusion
{
	// A decoded slice of the coarse stage. One coarse pixel = 256 native pixels = 7.68 km at 30 m/px.
	// Row-major, `width` columns per row.
	struct CoarseRegion
	{
		int32 width = 0;
		int32 height = 0;
		std::vector<float> elevation;   // metres above sea level; ocean clamped to 0 (as the pipeline does)
		std::vector<float> temperature; // degrees C
		std::vector<float> precip;      // mm/year
	};

	// BLOCKING and slow: ensures the model assets (downloading ~22 MB on first run), loads the coarse model,
	// and runs the 20-step sampler for every tile the region touches. Intended for bring-up and offline
	// dumps, not for per-frame use.
	// Coordinates are in coarse pixels and may be negative. Returns false on failure (already logged).
	bool sampleCoarseRegion(uint64 seed, int32 ci0, int32 cj0, int32 ci1, int32 cj1, CoarseRegion& out);

	// A decoded slice at NATIVE resolution (30 m/px in the shipped config). Same layout as CoarseRegion,
	// but elevation keeps its sign (negative = seabed) rather than clamping the ocean to 0.
	struct NativeRegion
	{
		int32 width = 0;
		int32 height = 0;
		std::vector<float> elevation;   // metres relative to sea level
		std::vector<float> temperature; // degrees C, lapse-corrected to this elevation
		std::vector<float> precip;      // mm/year
	};

	// The full coarse -> latent -> decoder chain. BLOCKING; the first call pulls 2.28 GB of models.
	// Coordinates are in native pixels (i = row, j = column) and may be negative.
	bool sampleNativeRegion(uint64 seed, int32 i1, int32 j1, int32 i2, int32 j2, NativeRegion& out);

	// Writes a shaded-relief PPM (ocean blue -> green -> brown -> snow) for eyeballing the result.
	// `hillshade` adds a simple directional shade, which is what actually makes 30 m/px relief readable.
	bool writeCoarsePpm(const CoarseRegion& region, const std::filesystem::path& path);
	bool writeNativePpm(const NativeRegion& region, const std::filesystem::path& path, bool hillshade = true);
}
