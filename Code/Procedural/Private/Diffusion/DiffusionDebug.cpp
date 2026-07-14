module Procedural;

import Core;
import Core.Log;
import :Diffusion.Debug;
import :Diffusion.ModelAssets;
import :Diffusion.Onnx;
import :Diffusion.Pipeline;
import :Diffusion.Tensor;

namespace Procedural::Diffusion
{
	namespace
	{
		// The models are 22 MB (coarse) to 2.28 GB (full), so they are loaded once per process and the
		// pipeline is reseeded rather than rebuilt. TerrainGenV3 will own this properly in M3; for now the
		// debug hook keeps its own.
		struct DebugRuntime
		{
			ModelAssets assets;
			std::unique_ptr<WorldPipeline> pipeline;
			bool coarseOnly = true;
			bool failed = false;
		};

		DebugRuntime& runtime()
		{
			static DebugRuntime r;
			return r;
		}

		std::mutex g_mutex; // the pipeline is not thread-safe

		bool ensurePipeline(uint64 seed, bool coarseOnly)
		{
			DebugRuntime& rt = runtime();

			// Widening coarse-only -> full needs the base/decoder models and a rebuilt stage graph.
			const bool needRebuild = !rt.pipeline || (rt.coarseOnly && !coarseOnly);
			if (needRebuild)
			{
				if (rt.failed)
					return false;

				if (!rt.assets.load(coarseOnly ? EAssetSet::CoarseOnly : EAssetSet::Full))
				{
					rt.failed = true;
					return false;
				}

				rt.pipeline = std::make_unique<WorldPipeline>();
				if (!rt.pipeline->initialize(seed, rt.assets.config(), rt.assets.data(),
				                             EInferenceDevice::Gpu, coarseOnly))
				{
					rt.pipeline.reset();
					rt.failed = true;
					return false;
				}
				rt.coarseOnly = coarseOnly;
			}
			rt.pipeline->setSeed(seed); // cheap; no-op when unchanged
			return true;
		}
	}

	bool sampleCoarseRegion(uint64 seed, int32 ci0, int32 cj0, int32 ci1, int32 cj1, CoarseRegion& out)
	{
		if (ci1 <= ci0 || cj1 <= cj0)
			return false;

		std::lock_guard<std::mutex> lk(g_mutex);
		if (!ensurePipeline(seed, /*coarseOnly*/ true))
			return false;

		WorldPipeline& p = *runtime().pipeline;
		const auto t0 = Clock::now();
		const FloatTensor slice = p.getCoarseSlice(ci0, cj0, ci1, cj1);
		const auto t1 = Clock::now();

		const int32 H = ci1 - ci0, W = cj1 - cj0;
		const size_t plane = (size_t)H * W;
		assert(slice.data.size() == 7 * plane);

		out.width = W;
		out.height = H;
		out.elevation.resize(plane);
		out.temperature.resize(plane);
		out.precip.resize(plane);

		const float* ch0 = slice.data.data() + 0 * plane; // elev_sqrt (weighted)
		const float* ch2 = slice.data.data() + 2 * plane; // temp
		const float* ch4 = slice.data.data() + 4 * plane; // precip
		const float* w = slice.data.data() + 6 * plane;   // blend weight

		for (size_t i = 0; i < plane; i++)
		{
			// Recover the weighted average, then undo the signed-sqrt elevation transform. Ocean clamps to
			// 0 here exactly as WorldPipeline::computeClimate does.
			const float wi = w[i];
			const float inv = (wi > 1e-6f) ? 1.0f / wi : 0.0f;
			const float e = std::max(0.0f, ch0[i] * inv);
			out.elevation[i] = e * e;
			out.temperature[i] = ch2[i] * inv;
			out.precip[i] = ch4[i] * inv;
		}

		float eMin = FLT_MAX, eMax = -FLT_MAX, tMin = FLT_MAX, tMax = -FLT_MAX, pMin = FLT_MAX, pMax = -FLT_MAX;
		size_t land = 0;
		for (size_t i = 0; i < plane; i++)
		{
			eMin = std::min(eMin, out.elevation[i]);
			eMax = std::max(eMax, out.elevation[i]);
			tMin = std::min(tMin, out.temperature[i]);
			tMax = std::max(tMax, out.temperature[i]);
			pMin = std::min(pMin, out.precip[i]);
			pMax = std::max(pMax, out.precip[i]);
			if (out.elevation[i] > 0.0f)
				land++;
		}
		Log::info(std::format("[Diffusion] coarse {}x{} in {} ms | elev {:.0f}..{:.0f} m | temp {:.1f}..{:.1f} C | "
		                      "precip {:.0f}..{:.0f} mm/yr | land {:.0f}% | tiles computed {}",
		                      W, H, std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
		                      eMin, eMax, tMin, tMax, pMin, pMax,
		                      100.0 * (double)land / (double)plane, p.totalComputedWindowCount()));
		return true;
	}

	bool sampleNativeRegion(uint64 seed, int32 i1, int32 j1, int32 i2, int32 j2, NativeRegion& out)
	{
		if (i2 <= i1 || j2 <= j1)
			return false;

		std::lock_guard<std::mutex> lk(g_mutex);
		if (!ensurePipeline(seed, /*coarseOnly*/ false))
			return false;

		WorldPipeline& p = *runtime().pipeline;
		std::vector<float> elev, climate;
		p.resetInferenceStats();
		const auto t0 = Clock::now();
		if (!p.get(i1, j1, i2, j2, /*withClimate*/ true, elev, climate))
		{
			Log::error("[Diffusion] pipeline get() failed");
			return false;
		}
		const auto t1 = Clock::now();
		Log::info(std::format("[Diffusion]   {}", p.inferenceStatsText()));

		const int32 H = i2 - i1, W = j2 - j1;
		const size_t plane = (size_t)H * W;
		assert(elev.size() == plane && climate.size() == 5 * plane);

		out.width = W;
		out.height = H;
		out.elevation = std::move(elev);
		out.temperature.assign(climate.begin(), climate.begin() + (ptrdiff_t)plane);
		out.precip.assign(climate.begin() + (ptrdiff_t)(2 * plane), climate.begin() + (ptrdiff_t)(3 * plane));

		float eMin = FLT_MAX, eMax = -FLT_MAX, tMin = FLT_MAX, tMax = -FLT_MAX, pMin = FLT_MAX, pMax = -FLT_MAX;
		size_t land = 0;
		for (size_t i = 0; i < plane; i++)
		{
			eMin = std::min(eMin, out.elevation[i]);
			eMax = std::max(eMax, out.elevation[i]);
			tMin = std::min(tMin, out.temperature[i]);
			tMax = std::max(tMax, out.temperature[i]);
			pMin = std::min(pMin, out.precip[i]);
			pMax = std::max(pMax, out.precip[i]);
			if (out.elevation[i] > 0.0f)
				land++;
		}
		Log::info(std::format("[Diffusion] native {}x{} in {} ms | elev {:.0f}..{:.0f} m | temp {:.1f}..{:.1f} C | "
		                      "precip {:.0f}..{:.0f} mm/yr | land {:.0f}% | tiles {} | cache {:.0f} MB",
		                      W, H, std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
		                      eMin, eMax, tMin, tMax, pMin, pMax,
		                      100.0 * (double)land / (double)plane, p.totalComputedWindowCount(),
		                      (double)p.residentCacheBytes() / (1024.0 * 1024.0)));
		return true;
	}

	bool writeCoarsePpm(const CoarseRegion& region, const std::filesystem::path& path)
	{
		if (region.width <= 0 || region.height <= 0)
			return false;

		std::error_code ec;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), ec);

		std::ofstream f(path, std::ios::binary);
		if (!f)
			return false;

		float eMax = 1.0f;
		for (float e : region.elevation)
			eMax = std::max(eMax, e);

		f << "P6\n" << region.width << " " << region.height << "\n255\n";
		std::vector<uint8> row((size_t)region.width * 3);
		for (int32 r = 0; r < region.height; r++)
		{
			for (int32 c = 0; c < region.width; c++)
			{
				const float e = region.elevation[(size_t)r * region.width + c];
				float rgb[3];
				if (e <= 0.0f)
				{
					rgb[0] = 0.05f; rgb[1] = 0.15f; rgb[2] = 0.45f; // ocean (elevation is clamped at 0)
				}
				else
				{
					// green -> brown -> white by normalised altitude
					const float t = std::min(1.0f, e / eMax);
					if (t < 0.5f)
					{
						const float k = t / 0.5f;
						rgb[0] = 0.25f + 0.35f * k; rgb[1] = 0.55f - 0.10f * k; rgb[2] = 0.20f + 0.05f * k;
					}
					else
					{
						const float k = (t - 0.5f) / 0.5f;
						rgb[0] = 0.60f + 0.40f * k; rgb[1] = 0.45f + 0.55f * k; rgb[2] = 0.25f + 0.75f * k;
					}
				}
				for (int32 k = 0; k < 3; k++)
					row[(size_t)c * 3 + k] = (uint8)(std::clamp(rgb[k], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
			f.write((const char*)row.data(), (std::streamsize)row.size());
		}
		return (bool)f;
	}

	bool writeNativePpm(const NativeRegion& region, const std::filesystem::path& path, bool hillshade)
	{
		if (region.width <= 0 || region.height <= 0)
			return false;

		std::error_code ec;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), ec);
		std::ofstream f(path, std::ios::binary);
		if (!f)
			return false;

		const int32 W = region.width, H = region.height;
		float eMax = 1.0f;
		for (float e : region.elevation)
			eMax = std::max(eMax, e);

		auto elevAt = [&](int32 r, int32 c) -> float
		{
			return region.elevation[(size_t)std::clamp(r, 0, H - 1) * W + std::clamp(c, 0, W - 1)];
		};

		f << "P6\n" << W << " " << H << "\n255\n";
		std::vector<uint8> row((size_t)W * 3);
		for (int32 r = 0; r < H; r++)
		{
			for (int32 c = 0; c < W; c++)
			{
				const float e = elevAt(r, c);
				float rgb[3];
				if (e <= 0.0f)
				{
					// Shallow shelf reads lighter than deep ocean.
					const float d = std::clamp(-e / 4000.0f, 0.0f, 1.0f);
					rgb[0] = 0.10f - 0.06f * d;
					rgb[1] = 0.28f - 0.18f * d;
					rgb[2] = 0.55f - 0.20f * d;
				}
				else
				{
					const float t = std::min(1.0f, e / eMax);
					if (t < 0.5f)
					{
						const float k = t / 0.5f;
						rgb[0] = 0.25f + 0.35f * k; rgb[1] = 0.55f - 0.10f * k; rgb[2] = 0.20f + 0.05f * k;
					}
					else
					{
						const float k = (t - 0.5f) / 0.5f;
						rgb[0] = 0.60f + 0.40f * k; rgb[1] = 0.45f + 0.55f * k; rgb[2] = 0.25f + 0.75f * k;
					}
				}

				if (hillshade && e > 0.0f)
				{
					// Central-difference normal against a fixed NW light. 30 m/px cell size, so the
					// gradient is in metres per 60 m.
					const float dzdx = (elevAt(r, c + 1) - elevAt(r, c - 1)) / 60.0f;
					const float dzdy = (elevAt(r + 1, c) - elevAt(r - 1, c)) / 60.0f;
					const float len = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.0f);
					// light dir ~ normalize(-1, -1, 1.6)
					const float sh = std::clamp((0.4472f * dzdx + 0.4472f * dzdy + 0.7746f) / len, 0.0f, 1.0f);
					const float k = 0.45f + 0.85f * sh;
					for (float& v : rgb)
						v *= k;
				}
				for (int32 k = 0; k < 3; k++)
					row[(size_t)c * 3 + k] = (uint8)(std::clamp(rgb[k], 0.0f, 1.0f) * 255.0f + 0.5f);
			}
			f.write((const char*)row.data(), (std::streamsize)row.size());
		}
		return (bool)f;
	}
}
