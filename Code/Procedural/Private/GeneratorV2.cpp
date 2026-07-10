module Procedural;

import Core;
import Core.glm;
import :GeneratorV2;
import :Noise;

namespace
{
	uint32 hashCoord2(int32 x, int32 z, uint32 seed)
	{
		uint32 h = seed * 0x9E3779B9u;
		h ^= (uint32)x * 0x85EBCA6Bu;
		h ^= (uint32)z * 0xC2B2AE35u;
		h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
		return h;
	}
	float hash01(uint32 h) { return float(h & 0xFFFFFFu) * (1.0f / 16777215.0f); }

	std::atomic<uint32> s_v2InstanceCounter{ 0 };

	// Per-biome field table: pass 2 blends these across neighboring biome texels. Heights are relative to
	// sea level (Ocean digs the seabed; the flat sea-level water floods it). heightVar is the +- swing the
	// detail fBm applies on top — it doubles as the biome's "roughness" (swamps flat, taiga rugged).
	// reliefScale multiplies the pass-1.5 macro relief AND the fantasy peaks: flat-biased biomes keep
	// their smooth character while mountain-biased ones erupt (amplify by shape, not a flat multiplier).
	struct BiomeFieldParams { float baseHeight, heightVar, temperature, humidity, reliefScale; };
	constexpr BiomeFieldParams BIOME_TABLE[(size_t)Procedural::EBiomeV2::Count] =
	{
		// Base heights are OFFSETS on the continent altitude gradient (deep inland runs high), so any
		// biome — swamps, lakes — can sit at altitude; only Ocean is pinned below sea level.
		// baseH   var    temp   humidity relief
		{ -42.0f, 14.0f, 0.50f, 0.85f,   0.35f }, // Ocean (some seafloor drama -> occasional islands)
		{  10.0f,  7.0f, 0.95f, 0.05f,   0.55f }, // Desert (mesa-ish)
		{   9.0f,  5.0f, 0.85f, 0.25f,   0.50f }, // Savanna
		{   1.5f,  2.0f, 0.60f, 0.95f,   0.10f }, // Swampland (dead flat, at whatever altitude it sits)
		{   8.0f,  5.0f, 0.55f, 0.45f,   0.30f }, // Grassland (smooth rolling)
		{  12.0f, 10.0f, 0.50f, 0.60f,   0.80f }, // Forest
		{  14.0f, 12.0f, 0.80f, 0.95f,   0.70f }, // Rainforest
		{  18.0f, 16.0f, 0.25f, 0.55f,   1.30f }, // Taiga (mountainous)
		{   7.0f,  5.0f, 0.10f, 0.35f,   0.90f }, // Tundra
		{  14.0f, 10.0f, 0.02f, 0.40f,   1.20f }, // Arctic (dramatic ranges)
		{  -6.0f,  1.5f, 0.50f, 1.00f,   0.08f }, // Lakes (depression: pass-3 fill floods it flat)
		{  90.0f,  8.0f, 0.30f, 0.45f,   0.90f }, // Highlands (elevated plateau, smooth on top)
	};
}

namespace Procedural
{
	// Pass-3 simulation tile: height deltas (channel/valley carve) + water presence, simulated once per
	// region and sampled bilinearly. Only the unpadded interior is stored; both fields are feathered to
	// zero at the tile border, so adjacent tiles agree (both equal the pass-2 base there) without any
	// cross-tile simulation — rivers simply fade out near region borders for now.
	struct ErosionTileV2
	{
		static constexpr int32 RES = 128; // stored cells per side (unpadded)
		static constexpr int32 PAD = 32;  // simulation margin so flow can enter/leave the stored region
		std::vector<float> deltaH;        // RES*RES, <= 0 (carve)
		std::vector<float> water;         // RES*RES, [0,1] river/lake presence
		std::vector<float> waterY;        // RES*RES, water surface relative to the base height (dry = -30)
		std::vector<float> flow01;        // RES*RES, river flow angle / 2pi in [0,1); < 0 = none
	};

	TerrainGenV2::TerrainGenV2(const TerrainConfigV2& cfg)
		: m_cfg(cfg)
		, m_instanceId(++s_v2InstanceCounter)
		, m_continent(cfg.seed + 11u)
		, m_warpA(cfg.seed + 127u)
		, m_warpB(cfg.seed + 251u)
		, m_altitude(cfg.seed + 307u)
		, m_peaks(cfg.seed + 353u)
		, m_mountain(cfg.seed + 431u)
		, m_detail(cfg.seed + 389u)
		, m_climT(cfg.seed + 521u)
		, m_climH(cfg.seed + 653u)
	{
		// Sanitize + derive: the texel follows the biome scale (size / resolution), so "Biome size" alone
		// controls the scale; a warp far larger than the biome shreds it into confetti.
		m_cfg.biomeSize = glm::max(m_cfg.biomeSize, 8.0f);
		m_cfg.biomeResolution = glm::clamp(m_cfg.biomeResolution, 4.0f, 128.0f);
		// 0.75 floor: a texel-corner sample sits 0.707 texels from its nearest texel center — any narrower
		// and some points fall outside every kernel (zero weight sum). 2.0 = the 4x4 gather's full reach.
		m_cfg.biomeBlend = glm::clamp(m_cfg.biomeBlend, 0.75f, 2.0f);
		m_cfg.borderWarp = glm::min(m_cfg.borderWarp, m_cfg.biomeSize * 2.0f);
		m_texelSize = glm::max(m_cfg.biomeSize / m_cfg.biomeResolution, 1.0f);
	}

	// Warped continentalness in [0,1]: ONE field decides where the oceans are (below oceanFraction) AND
	// how high the land runs (rising inland above it) — which is exactly why land always ramps down to
	// meet its coast. Two octaves only: the higher octaves are what used to scatter small ponds around.
	float TerrainGenV2::continentalness(double worldX, double worldZ) const
	{
		const float wf = 1.5f / m_cfg.biomeSize;
		const double wx = worldX + (double)(m_warpA.fbm((float)(worldX * wf), (float)(worldZ * wf), 3) * m_cfg.borderWarp);
		const double wz = worldZ + (double)(m_warpB.fbm((float)(worldX * wf + 37.2f), (float)(worldZ * wf + 11.9f), 3) * m_cfg.borderWarp);
		return m_continent.fbm((float)(wx * m_cfg.continentFrequency), (float)(wz * m_cfg.continentFrequency), 2) * 0.5f + 0.5f;
	}

	// --- Pass 1: the biome field ------------------------------------------------------------------------

	EBiomeV2 TerrainGenV2::biomeRaw(double worldX, double worldZ) const
	{
		// Ocean placement + the altitude gradient share ONE continentalness field (see continentalness()),
		// so coastlines and the inland rise always agree.
		if (continentalness(worldX, worldZ) < m_cfg.oceanFraction)
			return EBiomeV2::Ocean;

		// Domain-warp the cluster lookup so biome borders wiggle instead of tracing the lattice.
		const float wf = 1.5f / m_cfg.biomeSize;
		const double wx = worldX + (double)(m_warpA.fbm((float)(worldX * wf), (float)(worldZ * wf), 3) * m_cfg.borderWarp);
		const double wz = worldZ + (double)(m_warpB.fbm((float)(worldX * wf + 37.2f), (float)(worldZ * wf + 11.9f), 3) * m_cfg.borderWarp);

		// Land: the nearest jittered Voronoi site owns the cluster and picks one of the land biomes by
		// hash — purely random neighbors by design (desert next to tundra is fine).
		const double cs = (double)m_cfg.biomeSize;
		const int32 ccx = (int32)std::floor(wx / cs);
		const int32 ccz = (int32)std::floor(wz / cs);
		float bestDist = FLT_MAX;
		uint32 bestHash = 0;
		for (int32 dz = -1; dz <= 1; ++dz)
			for (int32 dx = -1; dx <= 1; ++dx)
			{
				const int32 cx = ccx + dx, cz = ccz + dz;
				const uint32 h = hashCoord2(cx, cz, m_cfg.seed ^ 0xB105F00Du);
				const double sx = ((double)cx + 0.5 + (double)(hash01(h) - 0.5f) * 0.8) * cs;
				const double sz = ((double)cz + 0.5 + (double)(hash01(h * 0x9E3779B9u + 1u) - 0.5f) * 0.8) * cs;
				const float dxF = (float)(wx - sx), dzF = (float)(wz - sz);
				const float dist = dxF * dxF + dzF * dzF;
				if (dist < bestDist)
				{
					bestDist = dist;
					bestHash = h;
				}
			}
		return (EBiomeV2)(1u + (bestHash >> 8) % ((uint32)EBiomeV2::Count - 1u));
	}

	EBiomeV2 TerrainGenV2::biomeAtTexel(int32 tx, int32 tz) const
	{
		// The biome field IS this texel quantization: one uint8 per texel, hard borders. Memoized per
		// thread — pass-2 blends re-read the same texels thousands of times across a chunk.
		struct Entry { uint32 id = 0; int32 tx = 0, tz = 0; EBiomeV2 biome = EBiomeV2::Ocean; };
		thread_local Entry cache[2048];
		Entry& e = cache[hashCoord2(tx, tz, m_instanceId) & 2047u];
		if (e.id == m_instanceId && e.tx == tx && e.tz == tz)
			return e.biome;

		const double ts = (double)m_texelSize;
		const EBiomeV2 biome = biomeRaw(((double)tx + 0.5) * ts, ((double)tz + 0.5) * ts);
		e = Entry{ m_instanceId, tx, tz, biome };
		return biome;
	}

	EBiomeV2 TerrainGenV2::sampleBiome(double worldX, double worldZ) const
	{
		const double ts = (double)m_texelSize;
		return biomeAtTexel((int32)std::floor(worldX / ts), (int32)std::floor(worldZ / ts));
	}

	// --- Pass 1.5: the altitude map ---------------------------------------------------------------------

	float TerrainGenV2::altitudeAtTexel(int32 ax, int32 az) const
	{
		struct Entry { uint32 id = 0; int32 ax = 0, az = 0; float alt = 0.0f; };
		thread_local Entry cache[2048];
		Entry& e = cache[hashCoord2(ax, az, m_instanceId ^ 0x51F15EEDu) & 2047u];
		if (e.id == m_instanceId && e.ax == ax && e.az == az)
			return e.alt;

		// Texel value = the biome-blended base elevation (oceans low, taiga high, ...) plus large-scale
		// relief — the macro landscape the fine height field rides on. The relief has two parts, both
		// scaled by the blended per-biome reliefScale (amplify by SHAPE, not a flat multiplier):
		//  - rolling fBm hills, and
		//  - fantasy peaks: a low-frequency ridged field through a threshold + power curve, so most land
		//    stays gentle while cleared ridge zones erupt into big ranges (Minecraft-style peaks spline).
		const double ts = (double)glm::max(m_cfg.altitudeTexelSize, 8.0f);
		const double cx = ((double)ax + 0.5) * ts, cz = ((double)az + 0.5) * ts;
		const FieldBlend blend = blendFields(cx, cz);

		// Continent-scale gradient: land climbs with continentalness above the ocean threshold (deep
		// inland runs high, ramping down to meet its coast) and the seabed deepens away from shore. Biome
		// bases ride this as offsets — high-altitude grassland/swamp deep inland is the intended outcome.
		//
		// The coast itself climbs on a FIXED shelf (+COAST_RISE over the first COAST_BAND of
		// continentalness), so the shoreline profile — and with it the visible ocean size — does NOT
		// change with the inland-rise tweak: the rise only lifts the interior beyond the shelf. (Without
		// the split, a small rise left a wide near-sea band that flooded on every relief dip.)
		constexpr float COAST_BAND = 0.08f;
		constexpr float COAST_RISE = 12.0f;
		const float c = continentalness(cx, cz);
		const float shelfEnd = glm::min(m_cfg.oceanFraction + COAST_BAND, 1.0f);
		const float gradient = glm::smoothstep(m_cfg.oceanFraction, shelfEnd, c) * COAST_RISE
			+ glm::smoothstep(shelfEnd, 1.0f, c) * m_cfg.inlandRise
			- (1.0f - glm::smoothstep(0.0f, m_cfg.oceanFraction, c)) * m_cfg.oceanDeepen;

		const float rolling = m_altitude.fbm((float)(cx * m_cfg.altitudeFrequency), (float)(cz * m_cfg.altitudeFrequency), m_cfg.altitudeOctaves) * m_cfg.altitudeAmplitude;
		const float ridge = m_peaks.ridged((float)(cx * m_cfg.peakFrequency), (float)(cz * m_cfg.peakFrequency), 3);
		const float peaks = std::pow(glm::smoothstep(m_cfg.peakThreshold, 1.0f, ridge), glm::max(m_cfg.peakSharpness, 0.1f)) * m_cfg.peakAmplitude;
		const float alt = blend.baseHeight + gradient + (rolling + peaks) * blend.reliefScale;

		e = Entry{ m_instanceId, ax, az, alt };
		return alt;
	}

	float TerrainGenV2::sampleAltitude(double worldX, double worldZ) const
	{
		// Separable bicubic B-spline over the 4x4 altitude texels: C2-smooth (no bilinear creases), which
		// is the whole point of the map — smooth macro height gradients.
		const double ts = (double)glm::max(m_cfg.altitudeTexelSize, 8.0f);
		const double ux = worldX / ts - 0.5, uz = worldZ / ts - 0.5;
		const int32 ix = (int32)std::floor(ux), iz = (int32)std::floor(uz);
		const float tx = (float)(ux - (double)ix), tz = (float)(uz - (double)iz);

		const auto bspline = [](float t, float w[4])
		{
			const float t2 = t * t, t3 = t2 * t;
			w[0] = (1.0f - 3.0f * t + 3.0f * t2 - t3) * (1.0f / 6.0f);
			w[1] = (4.0f - 6.0f * t2 + 3.0f * t3) * (1.0f / 6.0f);
			w[2] = (1.0f + 3.0f * t + 3.0f * t2 - 3.0f * t3) * (1.0f / 6.0f);
			w[3] = t3 * (1.0f / 6.0f);
		};
		float wx[4], wz[4];
		bspline(tx, wx);
		bspline(tz, wz);

		float alt = 0.0f;
		for (int32 j = 0; j < 4; ++j)
			for (int32 i = 0; i < 4; ++i)
				alt += altitudeAtTexel(ix - 1 + i, iz - 1 + j) * wx[i] * wz[j];
		return alt * m_cfg.heightScale;
	}

	// --- Pass 2: derived fields -------------------------------------------------------------------------

	TerrainGenV2::FieldBlend TerrainGenV2::blendFields(double worldX, double worldZ) const
	{
		// Gather the 4x4 biome texels around the point and blend their table entries with a smooth radial
		// kernel (~2 texels of support): fields transition smoothly ACROSS hard biome borders, at a width
		// set by the biome texel size.
		const double ts = (double)m_texelSize;
		const int32 tx0 = (int32)std::floor(worldX / ts - 0.5) - 1;
		const int32 tz0 = (int32)std::floor(worldZ / ts - 0.5) - 1;

		FieldBlend blend{ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
		float wsum = 0.0f;
		// The blend knob scales the kernel width (in texels) — and with it the gradient width of every
		// derived field: sharp Minecraft-y borders at low values, long soft transitions at 2 (the 4x4
		// gather's full reach).
		const float support = m_cfg.biomeBlend * (float)ts;
		for (int32 j = 0; j < 4; ++j)
			for (int32 i = 0; i < 4; ++i)
			{
				const int32 tx = tx0 + i, tz = tz0 + j;
				const float dx = (float)(((double)tx + 0.5) * ts - worldX);
				const float dz = (float)(((double)tz + 0.5) * ts - worldZ);
				const float w = 1.0f - glm::smoothstep(0.0f, support, glm::length(glm::vec2(dx, dz)));
				if (w <= 0.0f)
					continue;
				const BiomeFieldParams& p = BIOME_TABLE[(size_t)biomeAtTexel(tx, tz)];
				blend.temperature += p.temperature * w;
				blend.humidity += p.humidity * w;
				blend.baseHeight += p.baseHeight * w;
				blend.heightVar += p.heightVar * w;
				blend.reliefScale += p.reliefScale * w;
				wsum += w;
			}
		const float inv = wsum > 1e-5f ? 1.0f / wsum : 0.0f;
		blend.temperature *= inv;
		blend.humidity *= inv;
		blend.baseHeight *= inv;
		blend.heightVar *= inv;
		blend.reliefScale *= inv;
		return blend;
	}

	float TerrainGenV2::baseHeight(double worldX, double worldZ) const
	{
		// The smooth pass-1.5 altitude carries the macro landscape (biome base heights + large relief);
		// the fine field adds slope-damped fBm detail scaled by the blended per-biome roughness: swamps
		// stay flat while taiga rolls, and the character morphs across borders with the blend.
		const FieldBlend blend = blendFields(worldX, worldZ);
		const float detail = m_detail.fbmEroded((float)(worldX * m_cfg.detailFrequency), (float)(worldZ * m_cfg.detailFrequency), m_cfg.detailOctaves);
		const float alt = sampleAltitude(worldX, worldZ);
		float h = m_cfg.seaLevel + alt + blend.heightVar * detail * m_cfg.heightScale;

		// Altitude-aware mountain detail: high-frequency ridged relief that fades in with the MACRO
		// altitude, so the smooth B-spline ranges grow craggy ridgelines and side valleys while low
		// ground (grasslands, swamps) never sees any of it. This is what turns "large smooth hills"
		// into mountains — the fine map adding detail where the altitude map says "high".
		const float mask = glm::smoothstep(m_cfg.mountainMaskStart, glm::max(m_cfg.mountainMaskFull, m_cfg.mountainMaskStart + 1.0f), alt);
		if (mask > 0.001f && m_cfg.mountainDetailAmplitude > 0.0f)
		{
			const float ridge = m_mountain.ridged((float)(worldX * m_cfg.mountainDetailFrequency), (float)(worldZ * m_cfg.mountainDetailFrequency), m_cfg.mountainDetailOctaves);
			h += (ridge - 0.3f) * m_cfg.mountainDetailAmplitude * mask * m_cfg.heightScale; // -0.3: ridges rise AND valleys cut in
		}
		return h;
	}

	float TerrainGenV2::sampleHeight(double worldX, double worldZ) const
	{
		float h = baseHeight(worldX, worldZ);
		if (m_cfg.erosionEnabled && m_cfg.erosionStrength > 0.0f)
			h += erosionAt(worldX, worldZ).deltaH;
		return h;
	}

	float TerrainGenV2::sampleWaterHeight(double worldX, double worldZ) const
	{
		if (!m_cfg.erosionEnabled || m_cfg.erosionStrength <= 0.0f)
			return m_cfg.seaLevel;
		// The river/lake surface rides the pass-2 base; where dry, waterY sits far below ground and the
		// max() hands the water back to the sea (the wall between the two hides underground).
		return glm::max(m_cfg.seaLevel, baseHeight(worldX, worldZ) + erosionAt(worldX, worldZ).waterY);
	}

	float TerrainGenV2::sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const
	{
		const float base = baseHeight(worldX, worldZ);
		if (!m_cfg.erosionEnabled || m_cfg.erosionStrength <= 0.0f)
		{
			outWaterLevel = m_cfg.seaLevel;
			return base;
		}
		const ErosionSample e = erosionAt(worldX, worldZ);
		outWaterLevel = glm::max(m_cfg.seaLevel, base + e.waterY);
		return base + e.deltaH;
	}

	float TerrainGenV2::sampleFlowAngle01(double worldX, double worldZ) const
	{
		if (!m_cfg.erosionEnabled || m_cfg.erosionStrength <= 0.0f)
			return -1.0f;
		return erosionAt(worldX, worldZ).flow01;
	}

	float TerrainGenV2::sampleTemperature(double worldX, double worldZ) const
	{
		const FieldBlend blend = blendFields(worldX, worldZ);
		const float n = m_climT.fbm((float)(worldX * m_cfg.detailFrequency * 0.25), (float)(worldZ * m_cfg.detailFrequency * 0.25), 3);
		// Continent-scale hot/cold bands + altitude lapse: high ground runs cold regardless of biome
		// (snowy peaks, cold Highlands), and whole regions trend warm/cool at ~30km scale.
		const float bands = m_climT.fbm((float)(worldX * m_cfg.climateBandFrequency + 91.7), (float)(worldZ * m_cfg.climateBandFrequency - 33.1), 2) * m_cfg.climateBandAmplitude;
		const float lapse = glm::max(sampleAltitude(worldX, worldZ), 0.0f) * m_cfg.temperatureLapse;
		// Internals (biome table, bands, lapse) work in normalized [0,1]; the sampler contract is Celsius.
		const float t01 = glm::clamp(blend.temperature + n * m_cfg.climateNoise + bands - lapse, 0.0f, 1.0f);
		return TEMPERATURE_MIN_C + t01 * (TEMPERATURE_MAX_C - TEMPERATURE_MIN_C);
	}

	float TerrainGenV2::baseHumidity(double worldX, double worldZ) const
	{
		const FieldBlend blend = blendFields(worldX, worldZ);
		const float n = m_climH.fbm((float)(worldX * m_cfg.detailFrequency * 0.25 + 17.3), (float)(worldZ * m_cfg.detailFrequency * 0.25 + 5.1), 3);
		return glm::clamp(blend.humidity + n * m_cfg.climateNoise, 0.0f, 1.0f);
	}

	float TerrainGenV2::sampleHumidity(double worldX, double worldZ) const
	{
		float hum = baseHumidity(worldX, worldZ);
		if (m_cfg.erosionEnabled && m_cfg.erosionStrength > 0.0f)
			hum = glm::mix(hum, 1.0f, erosionAt(worldX, worldZ).waterBoost); // high humidity marks rivers/lakes
		return hum;
	}

	float TerrainGenV2::sampleFogThickness(double worldX, double worldZ) const
	{
		// Derived rather than independent: humid biomes (swamp/rainforest) — and pass-3 rivers/lakes,
		// which push humidity to 1 — carry the regional fog.
		return glm::smoothstep(0.6f, 0.95f, sampleHumidity(worldX, worldZ));
	}

	// --- Pass 3: water accumulation + erosion --------------------------------------------------------

	std::shared_ptr<const ErosionTileV2> TerrainGenV2::getErosionTile(int32 tileX, int32 tileZ) const
	{
		const uint64 key = ((uint64)(uint32)tileX << 32) | (uint64)(uint32)tileZ;
		{
			std::lock_guard<std::mutex> lk(m_erosionMutex);
			if (auto it = m_erosionTiles.find(key); it != m_erosionTiles.end())
				return it->second;
		}

		// Simulate outside the lock (deterministic: a rare duplicate build under contention just wastes
		// a little work; first insert wins below).
		constexpr int32 RES = ErosionTileV2::RES, PAD = ErosionTileV2::PAD, FULL = RES + 2 * PAD;
		constexpr int32 N = FULL * FULL;
		const double region = (double)glm::max(m_cfg.erosionRegionSize, 256.0f);
		const double cell = region / (double)RES;
		const double ox = (double)tileX * region - ((double)PAD - 0.5) * cell; // padded grid cell centers
		const double oz = (double)tileZ * region - ((double)PAD - 0.5) * cell;

		// 1. Sample the pass-2 base height into the padded grid.
		std::vector<float> h((size_t)N);
		for (int32 j = 0; j < FULL; ++j)
			for (int32 i = 0; i < FULL; ++i)
				h[(size_t)j * FULL + i] = baseHeight(ox + (double)i * cell, oz + (double)j * cell);

		// 2. Water accumulation: uniform rain on every cell, routed down the steepest descent, highest
		// cells first (a cell's outflow target is lower, so it is processed after all its inflows).
		std::vector<int32> order(N);
		for (int32 i = 0; i < N; ++i) order[i] = i;
		std::sort(order.begin(), order.end(), [&](int32 a, int32 b) { return h[a] != h[b] ? h[a] > h[b] : a < b; });

		constexpr int32 NB_DX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
		constexpr int32 NB_DZ[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
		std::vector<float> flux((size_t)N, 1.0f);
		std::vector<float> carve((size_t)N, 0.0f);
		std::vector<int32> down((size_t)N, -1); // steepest-descent target: the river FLOW direction
		const float fluxRef = 400.0f;   // cells of drainage where the carve saturates
		const float slopeRef = 0.08f;   // slope where the carve saturates
		for (const int32 idx : order)
		{
			const int32 ix = idx % FULL, iz = idx / FULL;
			int32 best = -1;
			float bestDrop = 0.0f;
			for (int32 n = 0; n < 8; ++n)
			{
				const int32 nx = ix + NB_DX[n], nz = iz + NB_DZ[n];
				if (nx < 0 || nz < 0 || nx >= FULL || nz >= FULL)
					continue;
				const int32 ni = nz * FULL + nx;
				const float dist = (float)cell * ((NB_DX[n] != 0 && NB_DZ[n] != 0) ? 1.41421356f : 1.0f);
				const float drop = (h[idx] - h[ni]) / dist;
				if (drop > bestDrop) { bestDrop = drop; best = ni; }
			}
			if (best < 0)
				continue; // pit: the depression fill below decides whether it is a lake
			down[idx] = best;
			flux[best] += flux[idx];

			// 3. Stream-power-ish channel carve: grows with drainage area and slope. Ocean floor is
			// skipped — the sea already covers it.
			if (h[idx] > m_cfg.seaLevel - 2.0f)
			{
				const float fluxN = glm::min(1.0f, std::sqrt(flux[idx] / fluxRef));
				const float slopeN = glm::clamp(bestDrop / slopeRef, 0.0f, 1.0f);
				carve[idx] = m_cfg.erosionStrength * fluxN * glm::mix(0.3f, 1.0f, slopeN);
			}
		}
		std::vector<float> eroded((size_t)N);
		for (int32 i = 0; i < N; ++i)
			eroded[i] = h[i] - carve[i];

		// 4. Depression fill (priority flood, from the grid border inward): every cell's spill level.
		// Standing water = fill above the eroded ground -> a lake.
		std::vector<float> fill((size_t)N);
		std::vector<uint8> visited((size_t)N, 0);
		using QE = std::pair<float, int32>;
		std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
		for (int32 i = 0; i < FULL; ++i)
		{
			const int32 edges[4] = { i, (FULL - 1) * FULL + i, i * FULL, i * FULL + (FULL - 1) };
			for (const int32 e : edges)
				if (!visited[e]) { visited[e] = 1; fill[e] = eroded[e]; pq.push({ fill[e], e }); }
		}
		while (!pq.empty())
		{
			const auto [level, idx] = pq.top();
			pq.pop();
			const int32 ix = idx % FULL, iz = idx / FULL;
			for (int32 n = 0; n < 8; ++n)
			{
				const int32 nx = ix + NB_DX[n], nz = iz + NB_DZ[n];
				if (nx < 0 || nz < 0 || nx >= FULL || nz >= FULL)
					continue;
				const int32 ni = nz * FULL + nx;
				if (visited[ni])
					continue;
				visited[ni] = 1;
				fill[ni] = glm::max(eroded[ni], level);
				pq.push({ fill[ni], ni });
			}
		}

		// 5. Water surfaces + flow: lakes sit at their spill level, rivers part-fill their channel; both
		// carry an absolute surface height. River cells also carry their FLOW direction (the steepest-
		// descent step). The surface + flow then dilate outward a few cells with a small decay per step —
		// the flat "plateau" past the banks that lets the terrain cut the shoreline (never shape a water
		// edge with its own falloff), and the flow field the banks inherit for the wave-direction shader.
		constexpr float DRY = -1.0e9f;
		std::vector<float> surface((size_t)N, DRY);
		std::vector<float> flowD((size_t)N, -1.0f);
		for (int32 idx = 0; idx < N; ++idx)
		{
			if (h[idx] < m_cfg.seaLevel + 1.0f)
				continue; // the sea is the water down there
			const float lakeDepth = fill[idx] - eroded[idx];
			if (lakeDepth > 0.05f)
				surface[idx] = fill[idx]; // lake: flat spill level, no directed flow
			if (flux[idx] > 100.0f && down[idx] >= 0)
			{
				surface[idx] = glm::max(surface[idx], eroded[idx] + carve[idx] * 0.5f); // half-filled channel
				const int32 ddx = (down[idx] % FULL) - (idx % FULL);
				const int32 ddz = (down[idx] / FULL) - (idx / FULL);
				flowD[idx] = glm::fract(std::atan2((float)ddz, (float)ddx) * (0.5f / 3.14159265f) + 1.0f);
			}
		}
		{
			std::vector<float> surfaceNext((size_t)N);
			std::vector<float> flowNext((size_t)N);
			for (int32 iter = 0; iter < 12; ++iter) // ~12 cells of plateau beyond the shoreline
			{
				for (int32 idx = 0; idx < N; ++idx)
				{
					float best = surface[idx];
					float bestFlow = flowD[idx];
					const int32 ix = idx % FULL, iz = idx / FULL;
					for (int32 n = 0; n < 8; ++n)
					{
						const int32 nx = ix + NB_DX[n], nz = iz + NB_DZ[n];
						if (nx < 0 || nz < 0 || nx >= FULL || nz >= FULL)
							continue;
						const int32 ni = nz * FULL + nx;
						const float cand = surface[ni] - 0.3f; // slight decay per cell keeps it a plateau
						if (cand > best) { best = cand; bestFlow = flowD[ni]; }
					}
					surfaceNext[idx] = best;
					flowNext[idx] = bestFlow;
				}
				surface.swap(surfaceNext);
				flowD.swap(flowNext);
			}
		}

		// 6. Store the feathered interior: deltas fade to zero (and water to "dry") toward the tile border
		// so neighboring tiles (which fade to the same pass-2 base) stay continuous without cross-tile
		// simulation — rivers fade out near region borders for now.
		auto tile = std::make_shared<ErosionTileV2>();
		tile->deltaH.resize((size_t)RES * RES);
		tile->water.resize((size_t)RES * RES);
		tile->waterY.resize((size_t)RES * RES);
		tile->flow01.resize((size_t)RES * RES);
		constexpr float FEATHER = 20.0f; // cells
		for (int32 j = 0; j < RES; ++j)
			for (int32 i = 0; i < RES; ++i)
			{
				const int32 src = (j + PAD) * FULL + (i + PAD);
				const size_t dst = (size_t)j * RES + i;
				const float edge = (float)glm::min(glm::min(i, RES - 1 - i), glm::min(j, RES - 1 - j));
				const float w = glm::smoothstep(0.0f, FEATHER, edge);

				const float lakeDepth = fill[src] - eroded[src];
				const float river = glm::smoothstep(100.0f, 800.0f, flux[src]);
				const float lake = glm::clamp(lakeDepth / 0.5f, 0.0f, 1.0f);
				// Below sea level the ocean is the water — don't double-mark it.
				const float aboveSea = glm::smoothstep(m_cfg.seaLevel - 1.0f, m_cfg.seaLevel + 1.0f, h[src]);

				tile->deltaH[dst] = (eroded[src] - h[src]) * w;
				tile->water[dst] = glm::max(river, lake) * aboveSea * w;
				tile->waterY[dst] = glm::mix(-30.0f, glm::clamp(surface[src] - h[src], -30.0f, 100.0f), w * aboveSea);
				tile->flow01[dst] = flowD[src];
			}

		std::lock_guard<std::mutex> lk(m_erosionMutex);
		if (m_erosionTiles.size() > 192)
		{
			// Fast flight leaves a trail of stale regions in the cache: evict tiles far from the CURRENT
			// query first (a wholesale clear would also dump the tiles under the camera and force costly
			// re-simulation hitches). Tiles regenerate deterministically if ever revisited.
			for (auto it2 = m_erosionTiles.begin(); it2 != m_erosionTiles.end(); )
			{
				const int32 tx2 = (int32)(uint32)(it2->first >> 32);
				const int32 tz2 = (int32)(uint32)(it2->first & 0xFFFFFFFFu);
				if (glm::max(glm::abs(tx2 - tileX), glm::abs(tz2 - tileZ)) > 5)
					it2 = m_erosionTiles.erase(it2);
				else
					++it2;
			}
			if (m_erosionTiles.size() > 192) // everything nearby (huge region size): fall back to clearing
				m_erosionTiles.clear();
		}
		auto [it, inserted] = m_erosionTiles.try_emplace(key, std::move(tile));
		return it->second; // first insert wins on a duplicate build
	}

	TerrainGenV2::ErosionSample TerrainGenV2::erosionAt(double worldX, double worldZ) const
	{
		const double region = (double)glm::max(m_cfg.erosionRegionSize, 256.0f);
		const int32 tx = (int32)std::floor(worldX / region);
		const int32 tz = (int32)std::floor(worldZ / region);
		const uint64 key = ((uint64)(uint32)tx << 32) | (uint64)(uint32)tz;

		// Thread-local last-tile memo: chunk workers hammer one tile at a time — skip the map + mutex.
		thread_local uint32 lastId = 0;
		thread_local uint64 lastKey = ~0ull;
		thread_local std::shared_ptr<const ErosionTileV2> lastTile;
		if (lastId != m_instanceId || lastKey != key)
		{
			lastTile = getErosionTile(tx, tz);
			lastId = m_instanceId;
			lastKey = key;
		}
		const ErosionTileV2& tile = *lastTile;

		// Bilinear over the stored interior (border cells are feathered to "dry", so clamping is exact).
		constexpr int32 RES = ErosionTileV2::RES;
		const double cell = region / (double)RES;
		const float fx = glm::clamp((float)((worldX - (double)tx * region) / cell - 0.5), 0.0f, (float)RES - 1.001f);
		const float fz = glm::clamp((float)((worldZ - (double)tz * region) / cell - 0.5), 0.0f, (float)RES - 1.001f);
		const int32 x0 = (int32)fx, z0 = (int32)fz;
		const float sx = fx - (float)x0, sz = fz - (float)z0;
		const auto lerp2 = [&](const std::vector<float>& f)
		{
			const float a = glm::mix(f[(size_t)z0 * RES + x0], f[(size_t)z0 * RES + x0 + 1], sx);
			const float b = glm::mix(f[(size_t)(z0 + 1) * RES + x0], f[(size_t)(z0 + 1) * RES + x0 + 1], sx);
			return glm::mix(a, b, sz);
		};
		ErosionSample s;
		s.deltaH = lerp2(tile.deltaH);
		s.waterBoost = lerp2(tile.water);
		s.waterY = lerp2(tile.waterY);
		// Angles wrap, so no bilinear: nearest cell (rivers inherit their bank flow via the dilation).
		s.flow01 = tile.flow01[(size_t)(int32)(fz + 0.5f) * RES + (int32)(fx + 0.5f)];
		return s;
	}
}
