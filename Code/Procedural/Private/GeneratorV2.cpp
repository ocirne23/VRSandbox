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

	std::atomic<uint32> s_v2InstanceCounter{ 0 };

	// The biome attractor table: (temperature, humidity) are each biome's COORDINATES in climate space —
	// the independently generated climate fields select biomes by proximity to them — and the rest is the
	// terrain character blended in. Heights are relative to sea level and ride the continent altitude
	// gradient as offsets. heightVar is the +- swing the detail fBm applies on top — the biome's
	// "roughness" (swamps flat, taiga rugged). reliefScale multiplies the macro relief AND the fantasy
	// peaks: flat-biased biomes keep their smooth character while mountain-biased ones erupt.
	struct BiomeFieldParams { float baseHeight, heightVar, temperature, humidity, reliefScale; };
	constexpr BiomeFieldParams BIOME_TABLE[(size_t)Procedural::EBiomeV2::Count] =
	{
		// baseH   var    temp   humidity relief
		{ -42.0f, 14.0f, 0.50f, 0.85f,   0.35f }, // Ocean (weighted by continentalness, not climate)
		{  10.0f,  7.0f, 0.95f, 0.05f,   0.55f }, // Desert (mesa-ish)
		{   9.0f,  5.0f, 0.85f, 0.25f,   0.50f }, // Savanna
		{   1.5f,  2.0f, 0.60f, 0.95f,   0.10f }, // Swampland (dead flat, at whatever altitude it sits)
		{   8.0f,  5.0f, 0.55f, 0.45f,   0.30f }, // Grassland (smooth rolling)
		{  12.0f, 10.0f, 0.50f, 0.60f,   0.80f }, // Forest
		{  14.0f, 12.0f, 0.80f, 0.95f,   0.70f }, // Rainforest
		{  18.0f, 16.0f, 0.25f, 0.55f,   1.30f }, // Taiga (mountainous)
		{   7.0f,  5.0f, 0.10f, 0.35f,   0.90f }, // Tundra
		{  14.0f, 10.0f, 0.02f, 0.40f,   1.20f }, // Arctic (dramatic ranges)
		{  -6.0f,  1.5f, 0.50f, 1.00f,   0.08f }, // Lakes (the ultra-humid attractor: sunken + dead flat)
		{  90.0f,  8.0f, 0.30f, 0.45f,   0.90f }, // Highlands (elevated plateau; gated to deep inland)
	};

	// Coastal shelf shared by the altitude gradient and the Highlands inland gate: the coast always climbs
	// a FIXED +COAST_RISE over the first COAST_BAND of continentalness above the ocean threshold, so the
	// shoreline profile (and the visible ocean size) never changes with the inland-rise tweak.
	constexpr float COAST_BAND = 0.08f;
	constexpr float COAST_RISE = 12.0f;
}

namespace Procedural
{
	TerrainGenV2::TerrainGenV2(const TerrainConfigV2& cfg)
		: m_cfg(cfg)
		, m_instanceId(++s_v2InstanceCounter)
		, m_continent(cfg.seed + 11u)
		, m_warpA(cfg.seed + 127u)
		, m_warpB(cfg.seed + 251u)
		, m_tempField(cfg.seed + 769u)
		, m_humField(cfg.seed + 907u)
		, m_altitude(cfg.seed + 307u)
		, m_peaks(cfg.seed + 353u)
		, m_mountain(cfg.seed + 431u)
		, m_detail(cfg.seed + 389u)
		, m_climT(cfg.seed + 521u)
		, m_climH(cfg.seed + 653u)
		, m_fogPatch(cfg.seed + 811u)
	{
		m_cfg.biomeSize = glm::max(m_cfg.biomeSize, 8.0f);
		// Kernel width floor: below ~0.1 the Gaussians underflow between attractors and the blend
		// degenerates to the nearest-row fallback everywhere (hard climate-space cells).
		m_cfg.biomeBlend = glm::clamp(m_cfg.biomeBlend, 0.1f, 3.0f);
		// A warp far larger than the biome scale shreds the climate regions into confetti.
		m_cfg.borderWarp = glm::min(m_cfg.borderWarp, m_cfg.biomeSize * 2.0f);
	}

	// --- The independent climate fields -----------------------------------------------------------------

	// ONE shared domain warp displaces all three field lookups, so coastlines and climate contours wiggle
	// together. Continentalness decides where the oceans are (below oceanFraction) AND how high the land
	// runs (rising inland above it) — which is why land always ramps down to meet its coast. Temperature
	// carries the continent-scale hot/cold bands (deserts cluster in hot bands); humidity is independent.
	TerrainGenV2::Climate TerrainGenV2::climateAt(double worldX, double worldZ) const
	{
		const float wf = 1.5f / m_cfg.biomeSize;
		const double wx = worldX + (double)(m_warpA.fbm((float)(worldX * wf), (float)(worldZ * wf), 3) * m_cfg.borderWarp);
		const double wz = worldZ + (double)(m_warpB.fbm((float)(worldX * wf + 37.2f), (float)(worldZ * wf + 11.9f), 3) * m_cfg.borderWarp);

		Climate cl;
		cl.c = m_continent.fbm((float)(wx * m_cfg.continentFrequency), (float)(wz * m_cfg.continentFrequency), 2) * 0.5f + 0.5f;

		// Climate wavelength ~4x the biome extent: one fBm sweep crosses several attractors in climate
		// space, so individual biome regions come out roughly biomeSize across.
		const double cf = 0.25 / (double)m_cfg.biomeSize;
		const float bands = m_climT.fbm((float)(worldX * m_cfg.climateBandFrequency + 91.7), (float)(worldZ * m_cfg.climateBandFrequency - 33.1), 2) * m_cfg.climateBandAmplitude;
		cl.t01 = glm::clamp(m_tempField.fbm((float)(wx * cf), (float)(wz * cf), 3) * 0.5f + 0.5f + bands, 0.0f, 1.0f);
		cl.h01 = glm::clamp(m_humField.fbm((float)(wx * cf + 71.3), (float)(wz * cf - 19.7), 3) * 0.5f + 0.5f, 0.0f, 1.0f);
		return cl;
	}

	// --- Terrain character from the climate -------------------------------------------------------------

	// Gaussian-weighted blend of the biome attractors in (t01, h01) climate space: terrain character
	// morphs smoothly wherever the climate fields drift between attractors, at a softness set by
	// biomeBlend. Ocean is spatial (continentalness), mixed in across a narrow coastal band. Highlands'
	// weight is additionally gated by inland-ness so plateaus only rise deep in the interior.
	TerrainGenV2::FieldBlend TerrainGenV2::blendFields(const Climate& cl) const
	{
		const float sigma = 0.10f * m_cfg.biomeBlend;
		const float invS2 = 1.0f / (2.0f * sigma * sigma);
		const float shelfEnd = glm::min(m_cfg.oceanFraction + COAST_BAND, 1.0f);
		const float inland = glm::smoothstep(shelfEnd, 1.0f, cl.c);

		FieldBlend land{ 0.0f, 0.0f, 0.0f };
		float wsum = 0.0f;
		size_t nearest = (size_t)EBiomeV2::Grassland;
		float nearestD2 = FLT_MAX;
		for (size_t i = 1; i < (size_t)EBiomeV2::Count; ++i) // 0 = Ocean, handled below
		{
			const BiomeFieldParams& p = BIOME_TABLE[i];
			const float dT = cl.t01 - p.temperature;
			const float dH = cl.h01 - p.humidity;
			const float d2 = dT * dT + dH * dH;
			float w = std::exp(-d2 * invS2);
			if (i == (size_t)EBiomeV2::Highlands)
				w *= inland;
			else if (d2 < nearestD2) { nearestD2 = d2; nearest = i; }
			land.baseHeight += p.baseHeight * w;
			land.heightVar += p.heightVar * w;
			land.reliefScale += p.reliefScale * w;
			wsum += w;
		}
		if (wsum > 1e-12f)
		{
			const float inv = 1.0f / wsum;
			land.baseHeight *= inv; land.heightVar *= inv; land.reliefScale *= inv;
		}
		else // narrow kernel + climate far from every attractor: every Gaussian underflowed
		{
			const BiomeFieldParams& p = BIOME_TABLE[nearest];
			land = { p.baseHeight, p.heightVar, p.reliefScale };
		}

		const BiomeFieldParams& ocean = BIOME_TABLE[(size_t)EBiomeV2::Ocean];
		const float landW = glm::smoothstep(m_cfg.oceanFraction - 0.03f, m_cfg.oceanFraction + 0.03f, cl.c);
		return FieldBlend{
			glm::mix(ocean.baseHeight, land.baseHeight, landW),
			glm::mix(ocean.heightVar, land.heightVar, landW),
			glm::mix(ocean.reliefScale, land.reliefScale, landW),
		};
	}

	EBiomeV2 TerrainGenV2::sampleBiome(double worldX, double worldZ) const
	{
		const Climate cl = climateAt(worldX, worldZ);
		if (cl.c < m_cfg.oceanFraction)
			return EBiomeV2::Ocean;
		const float shelfEnd = glm::min(m_cfg.oceanFraction + COAST_BAND, 1.0f);
		const float inland = glm::smoothstep(shelfEnd, 1.0f, cl.c);
		size_t best = (size_t)EBiomeV2::Grassland;
		float bestW = -1.0f;
		for (size_t i = 1; i < (size_t)EBiomeV2::Count; ++i)
		{
			const BiomeFieldParams& p = BIOME_TABLE[i];
			const float dT = cl.t01 - p.temperature;
			const float dH = cl.h01 - p.humidity;
			float w = -(dT * dT + dH * dH); // nearest attractor (monotonic in the Gaussian weight)
			if (i == (size_t)EBiomeV2::Highlands && inland < 0.5f)
				continue;
			if (w > bestW) { bestW = w; best = i; }
		}
		return (EBiomeV2)best;
	}

	// --- The altitude map --------------------------------------------------------------------------------

	float TerrainGenV2::altitudeAtTexel(int32 ax, int32 az) const
	{
		struct Entry { uint32 id = 0; int32 ax = 0, az = 0; float alt = 0.0f; };
		thread_local Entry cache[2048];
		Entry& e = cache[hashCoord2(ax, az, m_instanceId ^ 0x51F15EEDu) & 2047u];
		if (e.id == m_instanceId && e.ax == ax && e.az == az)
			return e.alt;

		// Texel value = the climate-blended base elevation (oceans low, taiga high, ...) plus large-scale
		// relief — the macro landscape the fine height field rides on. The relief has two parts, both
		// scaled by the blended relief scale (amplify by SHAPE, not a flat multiplier):
		//  - rolling fBm hills, and
		//  - fantasy peaks: a low-frequency ridged field through a threshold + power curve, so most land
		//    stays gentle while cleared ridge zones erupt into big ranges.
		const double ts = (double)glm::max(m_cfg.altitudeTexelSize, 8.0f);
		const double cx = ((double)ax + 0.5) * ts, cz = ((double)az + 0.5) * ts;
		const Climate cl = climateAt(cx, cz);
		const FieldBlend blend = blendFields(cl);

		// Continent-scale gradient: the coast climbs the FIXED shelf, the inland rise only lifts the
		// interior beyond it (see COAST_BAND/COAST_RISE), and the seabed deepens away from shore.
		const float shelfEnd = glm::min(m_cfg.oceanFraction + COAST_BAND, 1.0f);
		const float gradient = glm::smoothstep(m_cfg.oceanFraction, shelfEnd, cl.c) * COAST_RISE
			+ glm::smoothstep(shelfEnd, 1.0f, cl.c) * m_cfg.inlandRise
			- (1.0f - glm::smoothstep(0.0f, m_cfg.oceanFraction, cl.c)) * m_cfg.oceanDeepen;

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

	// --- The fine fields ---------------------------------------------------------------------------------

	float TerrainGenV2::sampleHeight(double worldX, double worldZ) const
	{
		// The smooth altitude map carries the macro landscape; the fine field adds slope-damped fBm detail
		// scaled by the climate-blended roughness (swamps stay flat while taiga rolls), plus the
		// altitude-masked mountain detail: high-frequency ridged relief that fades in with the MACRO
		// altitude, so the smooth B-spline ranges grow craggy ridgelines and side valleys while low ground
		// (grasslands, swamps) never sees any of it.
		const FieldBlend blend = blendFields(climateAt(worldX, worldZ));
		const float detail = m_detail.fbmEroded((float)(worldX * m_cfg.detailFrequency), (float)(worldZ * m_cfg.detailFrequency), m_cfg.detailOctaves);
		const float alt = sampleAltitude(worldX, worldZ);
		float h = m_cfg.seaLevel + alt + blend.heightVar * detail * m_cfg.heightScale;

		const float mask = glm::smoothstep(m_cfg.mountainMaskStart, glm::max(m_cfg.mountainMaskFull, m_cfg.mountainMaskStart + 1.0f), alt);
		if (mask > 0.001f && m_cfg.mountainDetailAmplitude > 0.0f)
		{
			const float ridge = m_mountain.ridged((float)(worldX * m_cfg.mountainDetailFrequency), (float)(worldZ * m_cfg.mountainDetailFrequency), m_cfg.mountainDetailOctaves);
			h += (ridge - 0.3f) * m_cfg.mountainDetailAmplitude * mask * m_cfg.heightScale; // -0.3: ridges rise AND valleys cut in
		}
		return h;
	}

	float TerrainGenV2::sampleWaterHeight(double, double) const
	{
		return m_cfg.seaLevel; // no elevated water (rivers/lakes) modeled yet
	}

	float TerrainGenV2::sampleHeightAndWater(double worldX, double worldZ, float& outWaterLevel) const
	{
		outWaterLevel = m_cfg.seaLevel;
		return sampleHeight(worldX, worldZ);
	}

	float TerrainGenV2::sampleTemperature(double worldX, double worldZ) const
	{
		// The generative field (bands included) + fine jitter - altitude lapse: high ground runs cold
		// regardless of climate (snowy peaks, cold Highlands). The lapse deliberately does NOT feed back
		// into the field that drives terrain character (that would be circular through the altitude).
		const Climate cl = climateAt(worldX, worldZ);
		const float n = m_climT.fbm((float)(worldX * m_cfg.detailFrequency * 0.25), (float)(worldZ * m_cfg.detailFrequency * 0.25), 3);
		const float lapse = glm::max(sampleAltitude(worldX, worldZ), 0.0f) * m_cfg.temperatureLapse;
		// Internals work in normalized [0,1]; the sampler contract is Celsius.
		const float t01 = glm::clamp(cl.t01 + n * m_cfg.climateNoise - lapse, 0.0f, 1.0f);
		return TEMPERATURE_MIN_C + t01 * (TEMPERATURE_MAX_C - TEMPERATURE_MIN_C);
	}

	float TerrainGenV2::sampleHumidity(double worldX, double worldZ) const
	{
		const Climate cl = climateAt(worldX, worldZ);
		const float n = m_climH.fbm((float)(worldX * m_cfg.detailFrequency * 0.25 + 17.3), (float)(worldZ * m_cfg.detailFrequency * 0.25 + 5.1), 3);
		return glm::clamp(cl.h01 + n * m_cfg.climateNoise, 0.0f, 1.0f);
	}

	TerrainGenV2::FogSample TerrainGenV2::fogAt(double worldX, double worldZ) const
	{
		// Every contribution ramps over a WIDE band and they combine as a smooth union
		// (1 - prod(1 - x)) instead of max(): fog regions swell and thin gradually into each other —
		// no hard cluster edges, no creases where one source overtakes another.
		const Climate cl = climateAt(worldX, worldZ);
		// Broad humid fog: swamp/rainforest/wetland regions carry it (the smooth field, no jitter —
		// fog is km-scale). Neutral falloff: the global Fog/Height Falloff shapes it.
		const float humidFog = glm::smoothstep(0.5f, 1.0f, cl.h01);
		float falloffMul = 1.0f;

		// Grassland low fog: patches of ground-hugging morning mist where the climate sits near the
		// Grassland attractor (a wide, gentle climate radius). A dedicated patch noise (features ~1/4
		// of a biome across) keeps them COMMON without covering every meadow, ramping over most of the
		// noise range so patch cores are dense and edges trail off; the raised falloff is what makes
		// the fog "low" — density collapses within a few meters of the ground.
		float grassFog = 0.0f;
		if (m_cfg.grassFogAmount > 0.0f)
		{
			const BiomeFieldParams& g = BIOME_TABLE[(size_t)EBiomeV2::Grassland];
			const float dT = cl.t01 - g.temperature, dH = cl.h01 - g.humidity;
			const float grass = glm::smoothstep(0.30f, 0.08f, std::sqrt(dT * dT + dH * dH));
			const double pf = 1.0 / (double)m_cfg.biomeSize; // patch features ~1/4 biome (fbm's finer octaves)
			const float patch = glm::smoothstep(-0.05f, 0.65f, m_fogPatch.fbm((float)(worldX * pf + 3.7), (float)(worldZ * pf - 8.9), 2));
			grassFog = grass * patch;
			falloffMul += grassFog * 2.0f; // hug the ground
		}

		// Mountain-valley fog: the fine height carved BELOW the smooth macro altitude inside the mountain
		// mask is a valley floor between ridgelines — fill it, thinning gradually up the walls. The
		// moderate falloff raise keeps the fog pooled in the valley (ridges poke out clear).
		float valleyFog = 0.0f;
		if (m_cfg.valleyFogAmount > 0.0f)
		{
			const float alt = sampleAltitude(worldX, worldZ);
			const float mask = glm::smoothstep(m_cfg.mountainMaskStart, glm::max(m_cfg.mountainMaskFull, m_cfg.mountainMaskStart + 1.0f), alt);
			if (mask > 0.01f)
			{
				const float depth = (m_cfg.seaLevel + alt) - sampleHeight(worldX, worldZ); // > 0 = below the macro surface
				valleyFog = glm::smoothstep(1.0f, 30.0f, depth) * mask;
				falloffMul += valleyFog * 1.2f;
			}
		}

		const float thickness = 1.0f - (1.0f - humidFog)
			* (1.0f - grassFog * m_cfg.grassFogAmount)
			* (1.0f - valleyFog * m_cfg.valleyFogAmount);
		return FogSample{ thickness, glm::min(falloffMul, FOG_FALLOFF_MUL_MAX) };
	}

	float TerrainGenV2::sampleFogThickness(double worldX, double worldZ) const
	{
		return fogAt(worldX, worldZ).thickness;
	}

	float TerrainGenV2::sampleFogHeightFalloff(double worldX, double worldZ) const
	{
		return fogAt(worldX, worldZ).falloffMul;
	}
}
