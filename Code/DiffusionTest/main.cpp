// TEMPORARY M1 bring-up harness for the V3 / diffusion terrain stack.
//
// Verifies the three things M1 exists to de-risk:
//   1. DirectML initialises and runs alongside the rest of the engine's dependencies
//   2. the coarse stage produces plausible continents/oceans (dumps a PPM to eyeball)
//   3. the same seed reproduces bit-identical output (seed-consistency is the whole premise)
//
// Delete this target once TerrainGenV3 is wired into the App.

import Core;
import Core.glm;
import Core.Log;
import File;
import Procedural;

namespace
{
	using Procedural::EBiomeV2;
	constexpr EBiomeV2 kBiomes[] = {
		EBiomeV2::Desert, EBiomeV2::Savanna, EBiomeV2::Swampland, EBiomeV2::Grassland,
		EBiomeV2::Forest, EBiomeV2::Rainforest, EBiomeV2::Taiga, EBiomeV2::Tundra,
		EBiomeV2::Arctic, EBiomeV2::Lakes, EBiomeV2::Highlands,
	};
	constexpr const char* kNames[] = { "Desert", "Savanna", "Swampland", "Grassland", "Forest",
		"Rainforest", "Taiga", "Tundra", "Arctic(snow)", "Lakes", "Highlands" };

	// The same nearest-attractor rule the terrain shader splats with (BIOME_TABLE in GeneratorV2.cpp).
	// `humidity` is already [0,1]; `temperature` is Celsius.
	int nearestBiome(float temperature, float humidity)
	{
		const glm::vec2 c(Procedural::temperatureTo01(temperature), humidity);
		int best = 0;
		float bestD2 = FLT_MAX;
		for (size_t b = 0; b < ARRAY_SIZE(kBiomes); b++)
		{
			const glm::vec2 d = c - Procedural::biomeClimateCoords(kBiomes[b]);
			const float d2 = d.x * d.x + d.y * d.y;
			if (d2 < bestD2) { bestD2 = d2; best = (int)b; }
		}
		return best;
	}

	// Resolve each land pixel to the biome the terrain shader would splat. This is the check that matters
	// for "does it look right": it exercises the real table against the model's real climate instead of
	// trusting a hand-computed temperature threshold.
	void reportBiomeMix(std::string_view label, std::span<const float> elev, std::span<const float> temp,
	                    std::span<const float> precip)
	{
		int counts[ARRAY_SIZE(kBiomes)] = {};
		float snowLine = FLT_MAX;
		size_t land = 0;
		for (size_t i = 0; i < elev.size(); i++)
		{
			if (elev[i] <= 0.0f)
				continue;
			land++;
			const int best = nearestBiome(temp[i], Procedural::precipTo01(precip[i]));
			counts[best]++;
			if (kBiomes[best] == EBiomeV2::Arctic)
				snowLine = std::min(snowLine, elev[i]);
		}
		Log::info(std::format("[Test] {} - biome mix over {} land px (raw model climate):", label, land));
		for (size_t b = 0; b < ARRAY_SIZE(kBiomes); b++)
			if (counts[b] > 0)
				Log::info(std::format("[Test]     {:<12} {:5.1f}%", kNames[b],
				                      100.0 * (double)counts[b] / (double)std::max<size_t>(land, 1)));
		Log::info(std::format("[Test]     snow (Arctic) {}",
		                      snowLine == FLT_MAX ? std::string("never reached")
		                                          : std::format("from {:.0f} m up", snowLine)));
	}

	uint64 hashFloats(std::span<const float> v)
	{
		uint64 h = 1469598103934665603ull;
		for (float f : v)
		{
			uint32 bits = std::bit_cast<uint32>(f);
			for (int i = 0; i < 4; i++)
			{
				h ^= (uint64)((bits >> (i * 8)) & 0xFFu);
				h *= 1099511628211ull;
			}
		}
		return h;
	}
}

// Simulates exactly what HeightMapBaker does for the terrain-data map, through the real TerrainGenV3:
// two 512x512 cascades, near = Full, far = Coarse. This is the path that was thrashing the tile cache.
static int runBakeBench(uint64 seed, float metersPerPixel, bool runSlow)
{
	Procedural::TerrainConfigV3 cfg;
	cfg.seed = (uint32)seed;
	cfg.metersPerPixel = metersPerPixel;

	Log::info(std::format("[Bake] loading models (metersPerPixel = {:.1f})...", metersPerPixel));
	Procedural::TerrainGenV3 gen(cfg);
	while (!Procedural::TerrainGenV3::isReady())
	{
		if (Procedural::TerrainGenV3::hasFailed())
		{
			Log::error(std::format("[Bake] V3 failed: {}", Procedural::TerrainGenV3::statusText()));
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	constexpr uint32 RES = 512; // RendererVKLayout::FOG_TERRAIN_RES
	std::vector<Procedural::TerrainPoint> points((size_t)RES * RES);

	struct Case { const char* label; float range; Procedural::ESampleDetail detail; bool slow; };
	const Case cases[] = {
		{ "near cascade  2048 m  Full  ", 2048.0f,  Procedural::ESampleDetail::Full,   false },
		{ "far cascade  30000 m  Coarse", 30000.0f, Procedural::ESampleDetail::Coarse, false },
		// What the far cascade WOULD cost at full detail, i.e. the thing ESampleDetail exists to avoid.
		// Measured at 318 s against 1.3 s for the coarse path — a 249x difference, and the reason the
		// terrain-data map could never keep up. Opt-in ("slow") because it takes five minutes.
		{ "far cascade  30000 m  Full  ", 30000.0f, Procedural::ESampleDetail::Full,   true },
	};

	for (const Case& c : cases)
	{
		if (c.slow && !runSlow)
		{
			Log::info(std::format("[Bake] {} -> skipped (pass 'slow' to measure; ~5 min)", c.label));
			continue;
		}
		const double step = (double)c.range / (double)RES;
		const double origin = -(double)c.range * 0.5 + step * 0.5;
		const auto t0 = Clock::now();
		gen.sampleGrid(origin, origin, step, RES, RES, points, c.detail);
		const auto t1 = Clock::now();
		const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

		float eMin = FLT_MAX, eMax = -FLT_MAX;
		for (const Procedural::TerrainPoint& p : points)
		{
			eMin = std::min(eMin, p.height);
			eMax = std::max(eMax, p.height);
		}
		Log::info(std::format("[Bake] {} -> {:9.1f} ms  ({:.0f} m/texel, elev {:.0f}..{:.0f} m)",
		                      c.label, ms, step, eMin, eMax));
	}

	// Find some mountains to aim at: the origin is open ocean for plenty of seeds, and crag relief only
	// means anything on land. One cheap coarse sweep over ~600 km.
	double landX = 0.0, landZ = 0.0;
	{
		constexpr uint32 N = 128;
		const double range = 600000.0 * (double)metersPerPixel / 30.0; // ~600 km at true scale
		const double step = range / (double)N;
		const double origin = -range * 0.5;
		std::vector<Procedural::TerrainPoint> pts((size_t)N * N);
		gen.sampleGrid(origin, origin, step, N, N, pts, Procedural::ESampleDetail::Coarse);
		float best = -FLT_MAX;
		for (uint32 j = 0; j < N; j++)
			for (uint32 i = 0; i < N; i++)
			{
				const float h = pts[(size_t)j * N + i].height;
				if (h > best) { best = h; landX = origin + step * i; landZ = origin + step * j; }
			}
		Log::info(std::format("[Bake] tallest coarse point {:.0f} m at ({:.0f}, {:.0f})", best, landX, landZ));
	}

	// Crag relief (height - altitude) is what the terrain shader's rock layer keys on: it separates a
	// MOUNTAIN from flat ground at altitude. Terrain/Textures "Crag relief start/full" are the thresholds.
	// If this stays near zero, mountain tops keep the lowland biome's texture and never grow rock.
	{
		constexpr uint32 N = 256;
		const double range = 8000.0 * (double)metersPerPixel / 30.0;
		const double step = range / (double)N;
		std::vector<Procedural::TerrainPoint> pts((size_t)N * N);
		gen.sampleGrid(landX - range * 0.5, landZ - range * 0.5, step, N, N, pts, Procedural::ESampleDetail::Full);

		// What does the generator ACTUALLY report through the sampler interface? Every earlier check went
		// through the debug facade or compared near-vs-far DIFFERENCES, neither of which would notice a
		// field being uniformly wrong.
		{
			float tMin = FLT_MAX, tMax = -FLT_MAX, hMin = FLT_MAX, hMax = -FLT_MAX;
			float aMin = FLT_MAX, aMax = -FLT_MAX, yMin = FLT_MAX, yMax = -FLT_MAX;
			for (const Procedural::TerrainPoint& p : pts)
			{
				tMin = std::min(tMin, p.temperature); tMax = std::max(tMax, p.temperature);
				hMin = std::min(hMin, p.humidity);    hMax = std::max(hMax, p.humidity);
				aMin = std::min(aMin, p.altitude);    aMax = std::max(aMax, p.altitude);
				yMin = std::min(yMin, p.height);      yMax = std::max(yMax, p.height);
			}
			Log::info(std::format("[Bake] TerrainGenV3 reports: height {:.0f}..{:.0f} m | altitude {:.0f}..{:.0f} m",
			                      yMin, yMax, aMin, aMax));
			Log::info(std::format("[Bake]                        temperature {:.2f}..{:.2f} C | humidity {:.3f}..{:.3f}",
			                      tMin, tMax, hMin, hMax));
			if (tMin == tMax)
				Log::error(std::format("[Bake]   TEMPERATURE IS CONSTANT AT {:.2f} C (!!)", tMin));
		}

		double reliefMax = 0, reliefSum = 0;
		size_t land = 0, aboveStart = 0, aboveFull = 0;
		// The real Terrain/Textures defaults, and the SIGNED difference the shader actually uses:
		//   crag = smoothstep(cragStart, cragFull, (worldY - seaLevel) - altitude)
		// Signed, not abs: only ground standing PROUD of the macro surface is bare; a valley floor sits
		// below it and keeps its biome texture.
		constexpr double CRAG_START = 12.0, CRAG_FULL = 50.0;
		for (const Procedural::TerrainPoint& p : pts)
		{
			if (p.height <= 0.0f)
				continue;
			land++;
			const double relief = (double)p.height - (double)p.altitude; // signed, as the shader does
			reliefMax = std::max(reliefMax, relief);
			reliefSum += relief;
			if (relief >= CRAG_START) aboveStart++;
			if (relief >= CRAG_FULL) aboveFull++;
		}
		const double n = (double)std::max<size_t>(land, 1);
		Log::info(std::format("[Bake] crag relief (height - altitude) over {} land px:", land));
		Log::info(std::format("[Bake]   mean {:.1f} m  max {:.1f} m", reliefSum / n, reliefMax));
		Log::info(std::format("[Bake]   past crag start ({:.0f} m): {:.1f}%   full ({:.0f} m): {:.1f}% -> {}",
		                      CRAG_START, 100.0 * aboveStart / n, CRAG_FULL, 100.0 * aboveFull / n,
		                      aboveStart > 0 ? "rock will show" : "NO ROCK ANYWHERE (!!)"));

		// Does crag actually ADD anything over the slope test, or does it only fire where slope already
		// has? Reproduce the shader's blend exactly:
		//   slope = 1 - N.y ; rockW = max(smoothstep(slopeStart, slopeFull, slope), crag * 0.85)
		// and split the land by which term wins. "crag only" is the payload: rounded summits and exposed
		// ridge crests that are bare rock in reality but nowhere near steep enough for the slope test.
		//
		// MUST be sampled at the real LOD0 vertex spacing (chunkSize/lod0Res = 1 m). The slope the shader
		// sees is the MESH normal, which includes the procedural detail layer; measuring the gradient over
		// a coarse step smooths that away and reports slope as never firing, which is an artifact of the
		// measurement rather than of the terrain.
		{
			constexpr double SLOPE_START = 0.55, SLOPE_FULL = 0.75; // Terrain/Textures defaults
			// 2 m over 2 km: fine enough to resolve the detail layer (its shortest wavelength is 45 m) and
			// wide enough to cover real flanks. A small patch centred on the tallest COARSE pixel lands on a
			// summit plateau — that pixel is a 7.68 km average — and reports nothing firing anywhere.
			constexpr uint32 M = 1024;
			const double fineStep = 2.0;
			std::vector<Procedural::TerrainPoint> fine((size_t)M * M);
			gen.sampleGrid(landX - M * fineStep * 0.5, landZ - M * fineStep * 0.5, fineStep, M, M, fine,
			               Procedural::ESampleDetail::Full);

			auto smooth = [](double e0, double e1, double x)
			{
				const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
				return t * t * (3.0 - 2.0 * t);
			};
			size_t nSlope = 0, nCrag = 0, nBoth = 0, nNeither = 0, nCragWins = 0;
			double slopeDegMax = 0;
			for (uint32 j = 1; j + 1 < M; j++)
				for (uint32 i = 1; i + 1 < M; i++)
				{
					const Procedural::TerrainPoint& p = fine[(size_t)j * M + i];
					if (p.height <= 0.0f)
						continue;
					// Height-field normal: N = normalize(-dh/dx, 1, -dh/dz), so N.y = 1/sqrt(1+dx^2+dz^2).
					const double dhx = (fine[(size_t)j * M + i + 1].height - fine[(size_t)j * M + i - 1].height) / (2.0 * fineStep);
					const double dhz = (fine[(size_t)(j + 1) * M + i].height - fine[(size_t)(j - 1) * M + i].height) / (2.0 * fineStep);
					const double ny = 1.0 / std::sqrt(1.0 + dhx * dhx + dhz * dhz);
					slopeDegMax = std::max(slopeDegMax, std::acos(ny) * 180.0 / 3.14159265);
					const double slopeW = smooth(SLOPE_START, SLOPE_FULL, 1.0 - ny);
					const double cragW = smooth(CRAG_START, CRAG_FULL, (double)p.height - (double)p.altitude) * 0.85;

					const bool s = slopeW > 0.05, c = cragW > 0.05;
					if (s && c) { nBoth++; if (cragW > slopeW) nCragWins++; }
					else if (s) nSlope++;
					else if (c) nCrag++;
					else nNeither++;
				}
			const double m = (double)std::max<size_t>(nSlope + nCrag + nBoth + nNeither, 1);
			Log::info(std::format("[Bake] rock layer at {} m vertex spacing (steepest face {:.0f} deg; the "
			                      "slope test needs 63 deg to start):", fineStep, slopeDegMax));
			Log::info(std::format("[Bake]   slope only {:5.1f}%  |  CRAG ONLY {:5.1f}%  |  both {:5.1f}%  |  bare ground {:5.1f}%",
			                      100.0 * nSlope / m, 100.0 * nCrag / m, 100.0 * nBoth / m, 100.0 * nNeither / m));
			Log::info(std::format("[Bake]   where both fire, crag wins {:.1f}%", nBoth ? 100.0 * nCragWins / (double)nBoth : 0.0));
		}
	}

	// The thing the player actually sees: near (Full) and far (Coarse) must agree on CLIMATE for the same
	// world position, or the texture flips as the camera approaches and the position crosses the cascade
	// crossfade. Compare both levels over the same grid.
	{
		constexpr uint32 N = 128;
		const double range = 4000.0; // straddles the near/far handover
		const double step = range / (double)N;
		const double origin = -range * 0.5;
		std::vector<Procedural::TerrainPoint> full((size_t)N * N), coarse((size_t)N * N);
		gen.sampleGrid(origin, origin, step, N, N, full, Procedural::ESampleDetail::Full);
		gen.sampleGrid(origin, origin, step, N, N, coarse, Procedural::ESampleDetail::Coarse);

		double tMax = 0, tSum = 0, hMax = 0, hSum = 0, eMax = 0;
		int biomeDiffs = 0;
		for (size_t i = 0; i < full.size(); i++)
		{
			const double dt = std::abs(full[i].temperature - coarse[i].temperature);
			const double dh = std::abs(full[i].humidity - coarse[i].humidity);
			tMax = std::max(tMax, dt); tSum += dt;
			hMax = std::max(hMax, dh); hSum += dh;
			eMax = std::max(eMax, (double)std::abs(full[i].height - coarse[i].height));
			// Would the splat actually pick a different texture?
			if (nearestBiome(full[i].temperature, full[i].humidity) != nearestBiome(coarse[i].temperature, coarse[i].humidity))
				biomeDiffs++;
		}
		const double n = (double)full.size();
		Log::info(std::format("[Bake] near-vs-far agreement over {:.0f} m:", range));
		Log::info(std::format("[Bake]   temperature  mean {:.2f} C  max {:.2f} C", tSum / n, tMax));
		Log::info(std::format("[Bake]   humidity     mean {:.3f}    max {:.3f}", hSum / n, hMax));
		Log::info(std::format("[Bake]   height       max {:.0f} m (expected: the coarse level is smoothed)", eMax));
		Log::info(std::format("[Bake]   BIOME DIFFERS on {:.1f}% of texels -> {}",
		                      100.0 * biomeDiffs / n,
		                      biomeDiffs * 20 < (int)n ? "ok" : "TEXTURES WILL FLIP (!!)"));
	}

	Log::info("[Bake] done");
	return 0;
}

int main(int argc, char** argv)
{
	FileSystem::initialize(); // CWD becomes Assets/

	const uint64 seed = argc > 1 ? (uint64)std::strtoull(argv[1], nullptr, 10) : 1337ull;

	// "bake" mode uses TerrainGenV3 directly; the default mode uses the debug facade, which owns its own
	// pipeline. Running both in one process would load 2.28 GB of models TWICE.
	if (argc > 2 && std::string_view(argv[2]) == "bake")
		return runBakeBench(seed, argc > 3 ? (float)std::atof(argv[3]) : 3.0f,
		                    argc > 4 && std::string_view(argv[4]) == "slow");

	// 128x128 coarse pixels ~= 983 km across at 7.68 km per coarse pixel. Touches a 4x4 grid of 64-px
	// tiles (stride 48), i.e. 16 tiles x 20 sampler steps = 320 coarse model invocations.
	const int32 half = 64;
	Procedural::Diffusion::CoarseRegion region;

	Log::info(std::format("[Test] coarse region, seed {}", seed));
	if (!Procedural::Diffusion::sampleCoarseRegion(seed, -half, -half, half, half, region))
	{
		Log::error("[Test] sampleCoarseRegion FAILED");
		return 1;
	}

	const std::filesystem::path out = std::filesystem::path("Local") / "Diffusion" /
		std::format("coarse_seed{}.ppm", seed);
	if (!Procedural::Diffusion::writeCoarsePpm(region, out))
	{
		Log::error("[Test] writeCoarsePpm FAILED");
		return 1;
	}
	Log::info(std::format("[Test] wrote Assets/{}", out.generic_string()));

	// Determinism: the caches are already warm, so re-request and compare. This checks the blend/slice path
	// is stable; the cold-path check is the second process run (compare the reported hash between runs).
	Procedural::Diffusion::CoarseRegion again;
	if (!Procedural::Diffusion::sampleCoarseRegion(seed, -half, -half, half, half, again))
		return 1;

	const uint64 h1 = hashFloats(region.elevation);
	const uint64 h2 = hashFloats(again.elevation);
	Log::info(std::format("[Test] elevation hash {:016x} (repeat {:016x}) -> {}",
	                      h1, h2, h1 == h2 ? "IDENTICAL" : "DIFFERENT (!!)"));

	// A different seed must give a different world, or the seed isn't reaching the pipeline.
	Procedural::Diffusion::CoarseRegion other;
	if (!Procedural::Diffusion::sampleCoarseRegion(seed + 1, -half, -half, half, half, other))
		return 1;
	const uint64 h3 = hashFloats(other.elevation);
	Log::info(std::format("[Test] seed {} hash {:016x} -> {}", seed + 1, h3,
	                      h3 != h1 ? "differs from seed " + std::to_string(seed) + " (good)"
	                               : "SAME AS OTHER SEED (!!)"));

	// ---- M2: the full coarse -> latent -> decoder chain at native resolution -------------------------
	// Aim at real terrain rather than whatever happens to be at the origin: pick the tallest coarse pixel
	// in the map above, and convert it to native coordinates (1 coarse px = 256 native px).
	int32 bestI = 0, bestJ = 0;
	float bestE = -1.0f;
	for (int32 r = 0; r < region.height; r++)
		for (int32 c = 0; c < region.width; c++)
		{
			const float e = region.elevation[(size_t)r * region.width + c];
			if (e > bestE)
			{
				bestE = e;
				bestI = (r - half) * 256; // coarse index -> native pixel
				bestJ = (c - half) * 256;
			}
		}
	Log::info(std::format("[Test] tallest coarse pixel {:.0f} m at native ({}, {})", bestE, bestJ, bestI));

	// 512x512 native pixels = 15.36 km across at 30 m/px.
	const int32 n = 256;
	Procedural::Diffusion::NativeRegion nat;
	Log::info("[Test] native region (full pipeline)");
	if (!Procedural::Diffusion::sampleNativeRegion(seed, bestI - n, bestJ - n, bestI + n, bestJ + n, nat))
	{
		Log::error("[Test] sampleNativeRegion FAILED");
		return 1;
	}
	const std::filesystem::path natOut = std::filesystem::path("Local") / "Diffusion" /
		std::format("native_seed{}.ppm", seed);
	if (!Procedural::Diffusion::writeNativePpm(nat, natOut, true))
		return 1;
	Log::info(std::format("[Test] wrote Assets/{}", natOut.generic_string()));

	// Overlapping requests must agree where they overlap, or the tile blending is wrong. Ask for a shifted
	// window and compare the shared area against the first result. (Exact equality isn't expected: the
	// Laplacian denoise pass sees a different padded window, so the low band shifts by a hair.)
	Procedural::Diffusion::NativeRegion shifted;
	if (!Procedural::Diffusion::sampleNativeRegion(seed, bestI - n + 64, bestJ - n + 64,
	                                               bestI + n + 64, bestJ + n + 64, shifted))
		return 1;
	double maxDiff = 0.0;
	for (int32 r = 0; r < 2 * n - 64; r++)
		for (int32 c = 0; c < 2 * n - 64; c++)
		{
			const float a = nat.elevation[(size_t)(r + 64) * nat.width + (c + 64)];
			const float b = shifted.elevation[(size_t)r * shifted.width + c];
			maxDiff = std::max(maxDiff, (double)std::abs(a - b));
		}
	Log::info(std::format("[Test] overlap agreement: max |diff| = {:.4f} m -> {}", maxDiff,
	                      maxDiff < 0.5 ? "consistent" : "INCONSISTENT (!!)"));

	// Does the real model climate land on sensible biomes? Check BOTH scales: a whole continent (does the
	// table cover the full climate range, or does everything collapse onto one attractor?) and the high
	// mountain region above (does snow appear at all?).
	reportBiomeMix("continent (983 km)", region.elevation, region.temperature, region.precip);
	reportBiomeMix("mountains (15 km)", nat.elevation, nat.temperature, nat.precip);

	Log::info("[Test] done");
	return (h1 == h2 && h3 != h1 && maxDiff < 0.5) ? 0 : 2;
}
