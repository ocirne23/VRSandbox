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
	// A copy of the ground/rock climate boxes in TerrainStreamer.cpp, in the same real units, so this
	// harness can answer the one question flying around cannot: is every entry actually REACHABLE in the
	// model's climate, or is some texture dead? (That is exactly how the "no snow anywhere" bug hid: an
	// entry parked at a temperature the model never produces.) Kept deliberately dumb and flat — if the
	// real table moves and this does not, the percentages below stop matching what you see, which is the
	// worst failure mode a checker can have. Re-sync it when you edit the real one.
	struct Entry { const char* name; bool rock; float t0, t1, p0, p1; };
	constexpr float C_ANY0 = -1000.0f, C_ANY1 = 1000.0f, P_ANY0 = 0.0f, P_ANY1 = 100000.0f;
	constexpr Entry kEntries[] = {
		{ "scree",      false, C_ANY0,  -4.0f,  P_ANY0,  P_ANY1 },
		{ "tundra",     false,  -6.0f,   0.0f,   250.0f, P_ANY1 },
		{ "taiga",      false,  -1.0f,   5.0f,   300.0f, 1500.0f },
		{ "dry steppe", false,   2.0f,  19.0f,  P_ANY0,   400.0f },
		{ "grassland",  false,   4.0f,  20.0f,   400.0f, 1000.0f },
		{ "temp forest",false,   5.0f,  18.0f,  1000.0f, 1900.0f },
		{ "temp rainf", false,   3.0f,  15.0f,  1800.0f, P_ANY1 },
		{ "sand",       false,  20.0f, C_ANY1, P_ANY0,   300.0f },
		{ "savanna",    false,  19.0f, C_ANY1,  250.0f, 1300.0f },
		{ "trop forest",false,  18.0f, C_ANY1, 1300.0f, 2100.0f },
		{ "swamp",      false,  16.0f, C_ANY1, 2000.0f, P_ANY1 },
		{ "rock gray",  true,  C_ANY0,   1.0f, P_ANY0,  P_ANY1 },
		{ "rock mossy", true,    0.0f,  13.0f, 1100.0f, P_ANY1 },
		{ "rock temp",  true,    0.0f,  16.0f,  250.0f, 1100.0f },
		{ "rock worn",  true,   14.0f,  24.0f,  200.0f,  900.0f },
		{ "rock red",   true,   23.0f, C_ANY1, P_ANY0,   450.0f },
		{ "rock dark",  true,   15.0f, C_ANY1,  900.0f, P_ANY1 },
	};
	// Mirrors the Terrain/Textures defaults.
	constexpr float kClimateBlend = 0.09f;
	constexpr float kSnowTempFull = -11.0f, kSnowTempNone = -3.0f, kSnowAridity = 0.10f;

	float smoothstepf(float e0, float e1, float x)
	{
		const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// climateBoxWeight() from instanced_indirect.fs.glsl.
	float boxWeight(glm::vec2 c, const Entry& e, float precipFullMm, float invS2)
	{
		const float invFull = 1.0f / precipFullMm;
		const glm::vec2 lo(Procedural::temperatureTo01(e.t0), std::clamp(e.p0 * invFull, 0.0f, 1.0f));
		const glm::vec2 hi(Procedural::temperatureTo01(e.t1), std::clamp(e.p1 * invFull, 0.0f, 1.0f));
		const glm::vec2 d = glm::max(glm::max(lo - c, c - hi), glm::vec2(0.0f));
		return std::exp(-glm::dot(d, d) * invS2);
	}

	// Which ground entry the shader's pickClimate() would land on. `humidity` is already [0,1].
	int pickGround(float temperature, float humidity)
	{
		const float invS2 = 1.0f / (2.0f * kClimateBlend * kClimateBlend);
		const glm::vec2 c(Procedural::temperatureTo01(temperature), std::clamp(humidity, 0.0f, 1.0f));
		int best = -1;
		float w0 = -1.0f;
		for (size_t e = 0; e < ARRAY_SIZE(kEntries); e++)
		{
			if (kEntries[e].rock)
				continue;
			const float w = boxWeight(c, kEntries[e], Procedural::HUMIDITY_FULL_PRECIP_MM, invS2);
			if (w > w0) { best = (int)e; w0 = w; }
		}
		return best;
	}

	// Which entry the shader's pickClimate() would land on, plus how contested the pick was.
	void reportSplatMix(std::string_view label, std::span<const float> elev, std::span<const float> temp,
	                    std::span<const float> precip, float precipFullMm = 2200.0f)
	{
		const float invS2 = 1.0f / (2.0f * kClimateBlend * kClimateBlend);
		int counts[ARRAY_SIZE(kEntries)] = {};
		size_t land = 0, mush = 0, snowPx = 0;
		double snowSum = 0.0;
		float snowLine = FLT_MAX;
		for (size_t i = 0; i < elev.size(); i++)
		{
			if (elev[i] <= 0.0f)
				continue;
			land++;
			const glm::vec2 c(Procedural::temperatureTo01(temp[i]),
			                  std::clamp(precip[i] / precipFullMm, 0.0f, 1.0f));
			int best = -1, best2 = -1;
			float w0 = -1.0f, w1 = -1.0f;
			for (size_t e = 0; e < ARRAY_SIZE(kEntries); e++)
			{
				if (kEntries[e].rock)
					continue;
				const float w = boxWeight(c, kEntries[e], precipFullMm, invS2);
				if (w > w0) { best2 = best; w1 = w0; best = (int)e; w0 = w; }
				else if (w > w1) { best2 = (int)e; w1 = w; }
			}
			counts[best]++;
			// Two entries both scoring ~1 means the pixel sits INSIDE both boxes: a permanent 50/50 blend
			// where neither texture is ever seen on its own. That is the failure mode of a box model, and
			// it is invisible from a screenshot (it just looks like a slightly muddy texture).
			if (w0 > 0.98f && w1 > 0.98f)
				mush++;

			// The snow overlay, on flat ground (slope shedding is geometry, not climate).
			const float snowW = (1.0f - smoothstepf(kSnowTempFull, kSnowTempNone, temp[i]))
			                  * smoothstepf(0.0f, kSnowAridity, c.y);
			snowSum += snowW;
			if (snowW > 0.5f) { snowPx++; snowLine = std::min(snowLine, elev[i]); }
		}
		const double invLand = 100.0 / (double)std::max<size_t>(land, 1);
		Log::info(std::format("[Test] {} - GROUND mix over {} land px (raw model climate):", label, land));
		for (size_t e = 0; e < ARRAY_SIZE(kEntries); e++)
			if (!kEntries[e].rock)
				Log::info(std::format("[Test]     {:<12} {:5.1f}%{}", kEntries[e].name,
				                      (double)counts[e] * invLand, counts[e] == 0 ? "   <-- DEAD" : ""));
		// "contested" counts pixels within ~1.3 C / ~40 mm of TWO boxes, i.e. sitting on a seam. Seams are
		// meant to be 50/50 — that is the transition. It is only a smell if it approaches 100%, which would
		// mean the boxes straddle rather than abut and no texture is ever seen alone.
		Log::info(std::format("[Test]     contested (on a seam between two boxes): {:.1f}%", (double)mush * invLand));
		Log::info(std::format("[Test]     SNOW cover {:.1f}% of land (mean weight {:.3f}), {}",
		                      (double)snowPx * invLand, snowSum / (double)std::max<size_t>(land, 1),
		                      snowLine == FLT_MAX ? std::string("never reached")
		                                          : std::format("from {:.0f} m up", snowLine)));

		// Where to put the snow line is the one number that cannot be reasoned out: "below freezing" is NOT
		// permanently snow-covered (Siberia has a -10 C mean and is forest), so print what the model's
		// temperature actually does and what each candidate threshold would bury.
		std::vector<float> temps;
		temps.reserve(land);
		for (size_t i = 0; i < elev.size(); i++)
			if (elev[i] > 0.0f)
				temps.push_back(temp[i]);
		std::sort(temps.begin(), temps.end());
		const auto pct = [&](double p) { return temps[std::min(temps.size() - 1, (size_t)(p * 0.01 * (double)temps.size()))]; };
		if (!temps.empty())
		{
			Log::info(std::format("[Test]     land temperature percentiles: p1 {:.1f}  p5 {:.1f}  p10 {:.1f}  "
			                      "p25 {:.1f}  p50 {:.1f}  p90 {:.1f} C", pct(1), pct(5), pct(10), pct(25), pct(50), pct(90)));
			for (const float none : { 2.0f, 0.0f, -2.0f, -4.0f, -6.0f })
			{
				size_t n = 0;
				for (float t : temps)
					if (t < none)
						n++;
				Log::info(std::format("[Test]       snow-none at {:5.1f} C -> {:4.1f}% of land holds some snow",
				                      none, (double)n * invLand));
			}
		}

		// Rock is chosen independently, and only where the slope/crag layer exposes it — so report its mix
		// over the same pixels rather than pretending it competes with the ground.
		int rc[ARRAY_SIZE(kEntries)] = {};
		for (size_t i = 0; i < elev.size(); i++)
		{
			if (elev[i] <= 0.0f)
				continue;
			const glm::vec2 c(Procedural::temperatureTo01(temp[i]),
			                  std::clamp(precip[i] / precipFullMm, 0.0f, 1.0f));
			int best = -1; float w0 = -1.0f;
			for (size_t e = 0; e < ARRAY_SIZE(kEntries); e++)
			{
				if (!kEntries[e].rock)
					continue;
				const float w = boxWeight(c, kEntries[e], precipFullMm, invS2);
				if (w > w0) { best = (int)e; w0 = w; }
			}
			rc[best]++;
		}
		Log::info(std::format("[Test] {} - ROCK type that would be exposed here:", label));
		for (size_t e = 0; e < ARRAY_SIZE(kEntries); e++)
			if (kEntries[e].rock)
				Log::info(std::format("[Test]     {:<12} {:5.1f}%{}", kEntries[e].name,
				                      (double)rc[e] * invLand, rc[e] == 0 ? "   <-- DEAD" : ""));
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
static int runBakeBench(uint64 seed, float metersPerPixel, bool runSlow, bool useFp16)
{
	Procedural::TerrainConfigV3 cfg;
	cfg.seed = (uint32)seed;
	cfg.metersPerPixel = metersPerPixel;
	cfg.useFp16 = useFp16;

	Log::info(std::format("[Bake] loading models (metersPerPixel = {:.1f}, {})...", metersPerPixel,
	                      useFp16 ? "fp16" : "fp32"));
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

	// Chunk meshing: what the terrain streamer's worker actually runs per chunk. This is the cost the
	// player feels as terrain popping in, and it is NOT inference — it is the per-point sampling around it,
	// which is why it deserves its own number rather than being inferred from the bake timings.
	{
		Procedural::ChunkParams cp;
		cp.chunkSize = 512.0f;
		cp.lod0Res = 512;
		cp.skirtDepth = 5.0f;
		Procedural::TerrainChunkMesh mesh;
		for (const uint32 lod : { 0u, 2u })
		{
			cp.lod = lod;
			cp.coord = { 60 + (int)lod, 60 };
			generateChunk(gen, cp, mesh); // warm: the SAME chunk, so the timed run below hits resident tiles
			const auto t0 = Clock::now();  // and measures meshing + sampling only, not inference
			generateChunk(gen, cp, mesh);
			const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
			const uint32 res = std::max(1u, cp.lod0Res >> lod);
			Log::info(std::format("[Bake] chunk mesh LOD{} {}x{} -> {:8.1f} ms  ({} verts)",
			                      lod, res, res, ms, mesh.positions.size()));
		}
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
		// Scaled exactly as TerrainStreamer scales them for V3: the tweak is in metres at the model's true
		// scale, and crag relief shrinks with metersPerPixel, so an unscaled pair reports a threshold the
		// engine never applies (at mpp=3 it claimed 48% rock where the engine gives ~100%).
		const double cragScale = (double)metersPerPixel / 30.0;
		const double CRAG_START = 12.0 * cragScale, CRAG_FULL = 50.0 * cragScale;
		std::vector<float> reliefs;
		reliefs.reserve(pts.size());
		for (const Procedural::TerrainPoint& p : pts)
		{
			if (p.height <= 0.0f)
				continue;
			land++;
			const double relief = (double)p.height - (double)p.altitude; // signed, as the shader does
			reliefs.push_back((float)relief);
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

		// Where to put the thresholds is the one thing that cannot be reasoned out: crag is now measured
		// against the 7.68 km coarse surface, so its scale is set by how far peaks stand above a 7.68 km
		// average, not by any authored number. Print the distribution and what candidate pairs would bury.
		{
			std::vector<float> cr = reliefs;
			std::sort(cr.begin(), cr.end());
			const auto pct = [&](double q) { return cr[std::min(cr.size() - 1, (size_t)(q * 0.01 * (double)cr.size()))]; };
			if (!cr.empty())
			{
				Log::info(std::format("[Bake]   crag percentiles (model m): p10 {:.0f}  p25 {:.0f}  p50 {:.0f}  "
				                      "p75 {:.0f}  p90 {:.0f}  p99 {:.0f}",
				                      pct(10) / cragScale, pct(25) / cragScale, pct(50) / cragScale,
				                      pct(75) / cragScale, pct(90) / cragScale, pct(99) / cragScale));
				Log::info("[Bake]   candidate Crag start/full -> mean rock weight (0.85 = saturated):");
				for (const auto& cand : { std::pair{ 12.0, 50.0 }, std::pair{ 100.0, 300.0 },
				                          std::pair{ 200.0, 500.0 }, std::pair{ 300.0, 700.0 },
				                          std::pair{ 400.0, 900.0 } })
				{
					double sum = 0;
					for (float v : reliefs)
						sum += smoothstepf((float)(cand.first * cragScale), (float)(cand.second * cragScale), v) * 0.85;
					Log::info(std::format("[Bake]     {:4.0f} / {:4.0f} -> {:.3f}", cand.first, cand.second,
					                      sum / (double)reliefs.size()));
				}
			}
		}

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

	// THE banding check. Climate boundaries read as tide marks when temperature is a function of elevation
	// alone: every box edge is then an isotherm, and an isotherm at constant elevation is a contour line.
	// Measuring that is fiddlier than it looks — a "narrow" elevation band still spans enough metres for the
	// lapse rate to dominate the spread, which reports ~7 C of variation whether the wander is on or not.
	// So diff the SAME points against a generator with the wander disabled: that isolates exactly what it
	// contributes, in degrees, and the lapse rate cancels.
	{
		Procedural::TerrainConfigV3 cfg0 = cfg;
		cfg0.climateNoiseAmplitudeC = 0.0f;
		const Procedural::TerrainGenV3 gen0(cfg0); // shares the loaded pipeline; only the shaping differs

		constexpr uint32 M = 512;
		const double fineStep = 8.0;
		const double org = -(double)M * fineStep * 0.5;
		std::vector<Procedural::TerrainPoint> a((size_t)M * M), b((size_t)M * M);
		gen.sampleGrid(landX + org, landZ + org, fineStep, M, M, a, Procedural::ESampleDetail::Full);
		gen0.sampleGrid(landX + org, landZ + org, fineStep, M, M, b, Procedural::ESampleDetail::Full);

		double dMin = 1e9, dMax = -1e9, dSum = 0, dSum2 = 0;
		size_t n = 0;
		for (size_t i = 0; i < a.size(); i++)
		{
			if (a[i].height <= 0.0f)
				continue;
			n++;
			const double d = (double)a[i].temperature - (double)b[i].temperature;
			dMin = std::min(dMin, d); dMax = std::max(dMax, d);
			dSum += d; dSum2 += d * d;
		}
		if (n > 8)
		{
			const double mean = dSum / (double)n;
			const double sd = std::sqrt(std::max(0.0, dSum2 / (double)n - mean * mean));
			// Convert to metres of boundary movement through the model's own lapse (~0.005 C per MODEL m),
			// then into world metres: that is how far up and down a range a transition actually rides.
			const double perModelM = 0.005;
			const double worldScale = (double)metersPerPixel / 30.0;
			Log::info(std::format("[Bake] climate wander over {} land px (vs the same field with it disabled):", n));
			Log::info(std::format("[Bake]   temperature {:+.2f} .. {:+.2f} C, sd {:.2f} C", dMin, dMax, sd));
			Log::info(std::format("[Bake]   -> boundaries ride +-{:.0f} world m ({:.0f} model m) up and down; "
			                      "{}", (dMax - dMin) * 0.5 / perModelM * worldScale,
			                      (dMax - dMin) * 0.5 / perModelM,
			                      sd < 0.05 ? "OFF: every boundary is a contour line" : "no longer a contour line"));
		}
	}

	// Is the ROCK transition itself a contour line? crag = mesh height - macro, and after the macro became
	// the 7.68 km coarse surface (consistency with the far cascade) that surface is nearly FLAT across one
	// mountain — which would make crag ~= height - constant, i.e. the rock boundary an elevation contour.
	// Correlation of crag against height says so directly: 1.0 = a pure contour line.
	{
		constexpr uint32 M = 512;
		const double fineStep = 8.0;
		const double org = -(double)M * fineStep * 0.5;
		std::vector<Procedural::TerrainPoint> pts((size_t)M * M);
		gen.sampleGrid(landX + org, landZ + org, fineStep, M, M, pts, Procedural::ESampleDetail::Full);
		double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
		size_t n = 0;
		for (const Procedural::TerrainPoint& p : pts)
		{
			if (p.height <= 0.0f)
				continue;
			const double h = p.height, c = (double)p.height - (double)p.altitude;
			n++; sx += h; sy += c; sxx += h * h; syy += c * c; sxy += h * c;
		}
		if (n > 8)
		{
			const double dn = (double)n;
			const double cov = sxy / dn - (sx / dn) * (sy / dn);
			const double sdx = std::sqrt(std::max(1e-9, sxx / dn - (sx / dn) * (sx / dn)));
			const double sdy = std::sqrt(std::max(1e-9, syy / dn - (sy / dn) * (sy / dn)));
			const double r = cov / (sdx * sdy);
			Log::info(std::format("[Bake] crag-vs-height correlation over {} land px: r = {:.4f} -> {}", n, r,
			                      r > 0.98 ? "the ROCK boundary IS an elevation contour"
			                               : "rock follows local shape, not height"));
		}
	}

	// Near vs far ROCK. The shader's crag test is (meshY - seaLevel) - altitude, and meshY always comes
	// from the FULL-detail mesh whichever cascade the pixel reads -- only `altitude` switches. So the honest
	// comparison is full height against each cascade's altitude, which is what this does.
	// The two altitudes are different SURFACES, not different samplings of one: near reports the model's
	// Laplacian lowres band, far reports the coarse level (macro == elev there, GeneratorV3.cpp ~line 419).
	{
		constexpr uint32 M = 256;
		const double fineStep = 8.0;
		const double org = -(double)M * fineStep * 0.5;
		std::vector<Procedural::TerrainPoint> full((size_t)M * M), coarse((size_t)M * M);
		gen.sampleGrid(landX + org, landZ + org, fineStep, M, M, full, Procedural::ESampleDetail::Full);
		gen.sampleGrid(landX + org, landZ + org, fineStep, M, M, coarse, Procedural::ESampleDetail::Coarse);

		auto smooth = [](double e0, double e1, double x)
		{
			const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
			return t * t * (3.0 - 2.0 * t);
		};
		// As the engine applies them (TerrainStreamer scales the tweak by V3's world scale).
		const double cragScale2 = (double)metersPerPixel / 30.0;
		const double CRAG_START = 12.0 * cragScale2, CRAG_FULL = 50.0 * cragScale2;
		double cragNearSum = 0, cragFarSum = 0, rockNearSum = 0, rockFarSum = 0;
		size_t land = 0, rockNearOnly = 0, rockFarOnly = 0;
		for (size_t i = 0; i < full.size(); i++)
		{
			if (full[i].height <= 0.0f)
				continue;
			land++;
			const double y = full[i].height; // the MESH height, both times
			const double cragNear = y - full[i].altitude;
			const double cragFar  = y - coarse[i].altitude;
			cragNearSum += cragNear; cragFarSum += cragFar;
			const double rN = smooth(CRAG_START, CRAG_FULL, cragNear) * 0.85;
			const double rF = smooth(CRAG_START, CRAG_FULL, cragFar) * 0.85;
			rockNearSum += rN; rockFarSum += rF;
			if (rN < 0.1 && rF > 0.5) rockFarOnly++;   // rocky at distance, bare up close
			if (rN > 0.5 && rF < 0.1) rockNearOnly++;
		}
		const double n = (double)std::max<size_t>(land, 1);
		Log::info(std::format("[Bake] near-vs-far ROCK over {:.0f} m of mountain ({} land px):", M * fineStep, land));
		Log::info(std::format("[Bake]   crag relief  near {:7.1f} m  |  far {:7.1f} m   (mean; same points, same mesh height)",
		                      cragNearSum / n, cragFarSum / n));
		Log::info(std::format("[Bake]   rock weight  near {:7.3f}    |  far {:7.3f}", rockNearSum / n, rockFarSum / n));
		Log::info(std::format("[Bake]   ROCKY FAR BUT BARE NEAR on {:.1f}% of texels{}", 100.0 * rockFarOnly / n,
		                      rockFarOnly * 20 > land ? "  <-- peaks lose their rock as you approach" : ""));
		Log::info(std::format("[Bake]   rocky near but bare far  on {:.1f}%", 100.0 * rockNearOnly / n));

		// CLIMATE over the same mountain. The other near-vs-far climate check samples the world ORIGIN,
		// which for most seeds is ocean or lowland — and if the two levels diverge with RELIEF, flat water
		// is exactly where they would agree. Re-ask it where the relief actually is.
		//
		// RAW is what each level bakes; CORRECTED is what the terrain shader actually shades with, after it
		// moves the baked temperature from the baked height to the MESH height using the baked lapse rate
		// (terrainFields in instanced_indirect.fs.glsl). Mirrored here because raw disagreement is EXPECTED
		// — the levels bake different heights — and only the corrected pair has to match.
		const float seaLevel = cfg.seaLevel;
		double tMax = 0, tSum = 0, hMax = 0, hSum = 0, cMax = 0, cSum = 0;
		size_t m = 0;
		for (size_t i = 0; i < full.size(); i++)
		{
			if (full[i].height <= 0.0f)
				continue;
			m++;
			const double dt = std::abs((double)full[i].temperature - (double)coarse[i].temperature);
			const double dh = std::abs((double)full[i].humidity - (double)coarse[i].humidity);
			tMax = std::max(tMax, dt); tSum += dt;
			hMax = std::max(hMax, dh); hSum += dh;

			// The mesh height is the FULL-detail surface whichever cascade a pixel reads.
			const double meshElev = std::max((double)full[i].height - seaLevel, 0.0);
			// p.lapseRate is per unit of the GENERATOR's vertical frame, so the world-metre delta converts
			// through vertScale — exactly what the shader does with u_terrainParams.y.
			const double vertScale = (double)metersPerPixel / 30.0; // heightScale is 1 here
			const auto corrected = [&](const Procedural::TerrainPoint& p)
			{
				const double bakedElev = std::max((double)p.height - seaLevel, 0.0);
				return (double)p.temperature + (double)p.lapseRate * (meshElev - bakedElev) / vertScale;
			};
			const double dc = std::abs(corrected(full[i]) - corrected(coarse[i]));
			cMax = std::max(cMax, dc); cSum += dc;
		}
		if (m > 0)
		{
			Log::info(std::format("[Bake]   CLIMATE on this mountain, RAW:       temperature mean {:.2f} C max {:.2f} C  |  "
			                      "humidity mean {:.3f} max {:.3f}", tSum / (double)m, tMax, hSum / (double)m, hMax));
			Log::info(std::format("[Bake]   CLIMATE on this mountain, CORRECTED: temperature mean {:.2f} C max {:.2f} C -> {}",
			                      cSum / (double)m, cMax,
			                      cMax < 1.0 ? "the cascades agree on what the shader shades"
			                                 : "STILL MISMATCHED (peaks will flip texture at the crossfade)"));
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
			if (pickGround(full[i].temperature, full[i].humidity) != pickGround(coarse[i].temperature, coarse[i].humidity))
				biomeDiffs++;
		}
		const double n = (double)full.size();
		Log::info(std::format("[Bake] near-vs-far agreement over {:.0f} m:", range));
		Log::info(std::format("[Bake]   temperature  mean {:.2f} C  max {:.2f} C", tSum / n, tMax));
		Log::info(std::format("[Bake]   humidity     mean {:.3f}    max {:.3f}", hSum / n, hMax));
		Log::info(std::format("[Bake]   height       max {:.0f} m (expected: the coarse level is smoothed)", eMax));
		Log::info(std::format("[Bake]   GROUND TEXTURE DIFFERS on {:.1f}% of texels -> {}",
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
	{
		bool slow = false, fp16 = false;
		for (int i = 3; i < argc; i++)
		{
			slow = slow || std::string_view(argv[i]) == "slow";
			fp16 = fp16 || std::string_view(argv[i]) == "fp16";
		}
		return runBakeBench(seed, argc > 3 ? (float)std::atof(argv[3]) : 3.0f, slow, fp16);
	}

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

	// THE correctness gate for the full pipeline. The native region is centred on the tallest coarse pixel,
	// so it MUST come out as high land — if the coarse stage says 4806 m and the native stage says the same
	// spot is deep ocean, the latent/decoder stages are producing garbage.
	//
	// This check exists because a plausible-looking one did not catch exactly that: with LATENT_TILE_STRIDE
	// at 48 the pipeline turned this mountain range into -4494 m of seabed, while the tile-overlap agreement
	// happily reported "consistent" (0.07 m) throughout. Overlapping windows agreeing with EACH OTHER says
	// nothing about them agreeing with reality; only an absolute check does.
	{
		float lo = FLT_MAX, hi = -FLT_MAX;
		size_t land = 0;
		for (float e : nat.elevation)
		{
			lo = std::min(lo, e); hi = std::max(hi, e);
			if (e > 0.0f) land++;
		}
		const double landPct = 100.0 * (double)land / (double)std::max<size_t>(nat.elevation.size(), 1);
		const bool ok = landPct > 50.0 && hi > 0.5f * bestE;
		Log::info(std::format("[Test] native-vs-coarse sanity: coarse peak {:.0f} m -> native {:.0f}..{:.0f} m, "
		                      "{:.0f}% land -> {}", bestE, lo, hi, landPct,
		                      ok ? "OK" : "FAILED (the native stages disagree with the coarse map)"));
		if (!ok)
			Log::error("[Test] the full pipeline is producing a different world than the coarse stage - "
			           "check LATENT_TILE_STRIDE / DECODER_TILE_STRIDE in WorldPipeline.cpp");
	}

	// Does the real model climate land on sensible biomes? Check BOTH scales: a whole continent (does the
	// table cover the full climate range, or does everything collapse onto one attractor?) and the high
	// mountain region above (does snow appear at all?).
	reportSplatMix("continent (983 km)", region.elevation, region.temperature, region.precip);
	reportSplatMix("mountains (15 km)", nat.elevation, nat.temperature, nat.precip);

	Log::info("[Test] done");
	return (h1 == h2 && h3 != h1 && maxDiff < 0.5) ? 0 : 2;
}
