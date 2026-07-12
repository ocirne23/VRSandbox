module Procedural;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;
import Core.Tweaks;

import RendererVK;
import File;

import :OceanRenderer;
import :TerrainSampler;

namespace Procedural
{
	OceanRenderer::~OceanRenderer()
	{
		// Free the RenderNode before its ObjectContainer, while the renderer/device are still alive.
		m_node = RenderNode();
		m_container.reset();
	}

	void OceanRenderer::initialize()
	{
		auto gridDirty = [this]() { m_gridDirty = true; };

		Tweak::boolean("Ocean", "Enabled", &m_enabled);
		Tweak::floatVar("Ocean", "Sea level (m)", &m_seaLevel, -10.0f, 10.0f, 0.1f);
		Tweak::floatVar("Ocean", "Ring cell (m)", &m_ringCell, 0.02f, 2.0f, 0.005f, gridDirty);
		Tweak::intVar("Ocean", "Ring resolution", &m_ringRes, 64, 512, 4.0f, gridDirty);
		Tweak::intVar("Ocean", "Rings", &m_rings, 1, 10, 1.0f, gridDirty);
		// Negative = displacement sampled finer than the ring's Nyquist (slight shimmer while moving);
		// with fixed-cell rings the default 0 is already motion-stable.
		Tweak::floatVar("Ocean", "Detail bias", &m_detailBias, -2.0f, 2.0f, 0.05f);

		// TMA/JONSWAP spectrum inputs (Horvath 2015); re-evaluated on the GPU every frame, so all live.
		Tweak::floatVar("Ocean/Waves", "Wind speed (m/s)", &m_windSpeed, 0.0f, 40.0f, 0.1f);
		Tweak::floatVar("Ocean/Waves", "Fetch (km)", &m_fetchKm, 1.0f, 2000.0f, 1.0f);
		Tweak::floatVar("Ocean/Waves", "Depth (m)", &m_depth, 1.0f, 500.0f, 0.5f);
		Tweak::floatVar("Ocean/Waves", "Wind angle (rad)", &m_windAngle, 0.0f, 6.2831853f, 0.01f);
		Tweak::floatVar("Ocean/Waves", "Amplitude scale", &m_amplitude, 0.0f, 4.0f, 0.01f);
		Tweak::floatVar("Ocean/Waves", "Choppiness", &m_choppiness, 0.0f, 2.5f, 0.01f);
		Tweak::floatVar("Ocean/Waves", "Normal strength", &m_normalStrength, 0.0f, 4.0f, 0.01f);
		Tweak::floatVar("Ocean/Waves", "Cascade 0 (m)", &m_cascadeSizes.x, 16.0f, 2000.0f, 1.0f);
		Tweak::floatVar("Ocean/Waves", "Cascade 1 (m)", &m_cascadeSizes.y, 4.0f, 500.0f, 0.5f);
		Tweak::floatVar("Ocean/Waves", "Cascade 2 (m)", &m_cascadeSizes.z, 1.0f, 100.0f, 0.1f);

		Tweak::color3("Ocean/Shading", "Absorption (1/m)", &m_absorption);
		Tweak::color3("Ocean/Shading", "Scatter color", &m_scatterColor);
		Tweak::floatVar("Ocean/Shading", "Scatter strength", &m_scatterStrength, 0.0f, 4.0f, 0.01f);
		Tweak::floatVar("Ocean/Shading", "Roughness", &m_roughness, 0.02f, 0.5f, 0.001f);
		// Sharper sun glints: sharpness biases the shading-normal mips finer (some shimmer past ~1.5),
		// filtering scales the roughness-widening variance terms (0 = raw sharp GGX, 1 = fully filtered).
		Tweak::floatVar("Ocean/Shading", "Glint sharpness", &m_glintSharpness, 0.0f, 3.0f, 0.05f);
		Tweak::floatVar("Ocean/Shading", "Glint filtering", &m_glintFilter, 0.0f, 2.0f, 0.05f);
		// Crest SSS (Sea of Thieves-style): sun shining through back-lit crests, scaled by wave height.
		Tweak::floatVar("Ocean/Shading", "SSS strength", &m_sssStrength, 0.0f, 4.0f, 0.01f);
		Tweak::floatVar("Ocean/Shading", "SSS power", &m_sssPower, 1.0f, 16.0f, 0.1f);
		Tweak::boolean("Ocean/Shading", "Hit lighting", &m_hitLighting); // lights on geometry seen through/mirrored in the water
		Tweak::color3("Ocean/Shading", "Foam color", &m_foamColor);
		// One instant-foam response (thresholds + softness) draws the crest foam AND injects the
		// turbulence field; decay/spread shape the wake's life, foam boost/turbidity its look.
		Tweak::floatVar("Ocean/Foam", "Fold bias", &m_foamBias, 0.0f, 1.2f, 0.01f);
		Tweak::floatVar("Ocean/Foam", "Break accel (g)", &m_foamBreakAccel, 0.05f, 1.5f, 0.01f);
		Tweak::floatVar("Ocean/Foam", "Softness", &m_foamSoftness, 0.02f, 2.0f, 0.01f);
		Tweak::floatVar("Ocean/Foam", "Turb decay", &m_foamDecay, 0.5f, 0.999f, 0.001f);
		Tweak::floatVar("Ocean/Foam", "Turb spread", &m_foamSpread, 0.0f, 4.0f, 0.05f);
		Tweak::floatVar("Ocean/Foam", "Foam boost", &m_foamBoost, 0.0f, 2.0f, 0.01f);
		Tweak::floatVar("Ocean/Foam", "Turbidity", &m_turbidity, 0.0f, 1.0f, 0.01f);

		// Shore interaction: only active while the terrain streamer is enabled (its height field is what
		// gets baked). Range/scale changes re-bake on the fly; the old map stays active meanwhile.
		Tweak::boolean("Ocean/Shore", "Shore interaction", &m_shoreEnabled);
		Tweak::floatVar("Ocean/Shore", "Range (m)", &m_shoreRange, 256.0f, 8192.0f, 32.0f);
		Tweak::floatVar("Ocean/Shore", "Shoal depth scale", &m_shoalScale, 0.0f, 0.5f, 0.005f);
		Tweak::floatVar("Ocean/Shore", "Shore foam depth (m)", &m_shoreFoamDepth, 0.0f, 8.0f, 0.05f);
		// Land cull: clipmap triangles buried deeper than this under the local water level (over their
		// whole footprint) are discarded in the vertex shaders — no displacement sampling, no raster.
		Tweak::floatVar("Ocean/Shore", "Cull margin (m)", &m_cullMargin, 0.0f, 20.0f, 0.1f);
	}

	void OceanRenderer::rebuildGrid()
	{
		m_gridDirty = false;
		m_node = RenderNode(); // release the previous grid first
		m_container.reset();

		// Geometry clipmap: ring 0 is a full NxN-cell grid at m_ringCell; each outer ring is a square
		// annulus at double the cell size whose hole is the previous ring's coverage. Per vertex, the
		// texcoord carries (ring cell size, morph weight): the vertex shaders read them to pick the
		// ring-matched displacement mip and to run the CDLOD boundary morph (over each ring's outer band,
		// odd vertices collapse onto the next ring's lattice and the mip blends +1, so adjacent rings meet
		// exactly — no stitching geometry needed).
		const int   N = glm::clamp(m_ringRes & ~3, 16, 1024); // multiple of 4: hole edges stay on the lattice
		const float c0 = glm::max(m_ringCell, 0.01f);
		const int   rings = glm::clamp(m_rings, 1, 12);
		constexpr float MORPH_BAND_START = 0.7f; // morph over the outer 30% of each ring

		std::vector<glm::vec3> positions;
		std::vector<glm::vec3> normals;
		std::vector<glm::vec3> texCoords;
		std::vector<uint32> indices;
		const size_t vertsPerRing = (size_t)(N + 1) * (N + 1);
		positions.reserve(vertsPerRing * rings);
		normals.reserve(vertsPerRing * rings);
		texCoords.reserve(vertsPerRing * rings);

		for (int r = 0; r < rings; ++r)
		{
			const float cell = c0 * float(1 << r);
			const float outerH = cell * float(N) * 0.5f;              // this ring's coverage half-extent
			const float holeH = r == 0 ? -1.0f : outerH * 0.5f;       // previous ring's coverage
			const uint32 base = (uint32)positions.size();

			for (int j = 0; j <= N; ++j)
			{
				for (int i = 0; i <= N; ++i)
				{
					const float x = float(i - N / 2) * cell;
					const float z = float(j - N / 2) * cell;
					const float cheb = glm::max(std::fabs(x), std::fabs(z));
					const float morph = glm::clamp((cheb - MORPH_BAND_START * outerH) / ((1.0f - MORPH_BAND_START) * outerH), 0.0f, 1.0f);
					positions.push_back(glm::vec3(x, 0.0f, z));
					normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
					texCoords.push_back(glm::vec3(cell, morph, 0.0f));
				}
			}
			for (int j = 0; j < N; ++j)
			{
				for (int i = 0; i < N; ++i)
				{
					// Skip cells fully inside the hole (covered by the finer ring).
					const float xMax = glm::max(std::fabs(float(i - N / 2)), std::fabs(float(i + 1 - N / 2))) * cell;
					const float zMax = glm::max(std::fabs(float(j - N / 2)), std::fabs(float(j + 1 - N / 2))) * cell;
					if (r > 0 && glm::max(xMax, zMax) <= holeH + cell * 0.25f)
						continue;
					const uint32 a = base + (uint32)(j * (N + 1) + i);
					const uint32 b = a + 1;
					const uint32 c = a + (uint32)(N + 1);
					const uint32 d = c + 1;
					indices.push_back(a); indices.push_back(c); indices.push_back(b);
					indices.push_back(b); indices.push_back(c); indices.push_back(d);
				}
			}
		}

		MeshGeometryDesc geom;
		geom.positions = positions.data();
		geom.normals = normals.data();
		geom.texCoords = texCoords.data();
		geom.numVertices = (uint32)positions.size();
		geom.indices = indices.data();
		geom.numIndices = (uint32)indices.size();
		geom.name = "Ocean";

		std::unique_ptr<ISceneData> scene = ISceneData::createMeshScene(geom);
		if (!scene)
			return;

		ObjectContainer::MaterialOverrides overrides;
		overrides.pipelineIdx = RendererVKLayout::EPipelineIndex::Ocean;
		overrides.useSceneTextures = true;
		overrides.excludeFromRayTracing = true; // the animated water surface isn't in the TLAS
		auto container = std::make_unique<ObjectContainer>();
		if (!container->initialize(*scene, &overrides))
			return;

		RenderNode node = container->spawnRootNode(
			Transform(glm::vec3(0.0f, m_seaLevel, 0.0f), 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
		m_container = std::move(container);
		m_node = std::move(node);
	}

	// Shore bake: an OCEAN_SHORE_RES^2 snapshot of (terrain height, water level) pairs covering
	// m_shoreRange meters around the camera (HeightMapBaker, sampled from the SAME terrain sampler the
	// terrain streamer renders from, so the water agrees with the drawn ground). The shaders derive the
	// water depth live as water level - height and lift the clipmap by water level - sea level (lakes/
	// rivers at altitude); beyond this map's range they fall back to the coarser fog terrain cascades
	// (TerrainStreamer's bake of the same fields).
	void OceanRenderer::updateShoreMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& terrain)
	{
		const bool active = m_shoreEnabled && terrain != nullptr;
		HeightMapBaker::Baked baked;
		if (m_shoreBaker.update(baked, active, terrain, glm::vec2(camera.position.x, camera.position.z),
			glm::vec2(glm::max(m_shoreRange, 256.0f), 0.0f), RendererVKLayout::OCEAN_SHORE_RES, 1, 4, true)) // shore layout: h, water, flow, spare
		{
			renderer.setOceanShoreMap(baked.texels, baked.center, baked.ranges.x);
			m_shoreHeights = std::move(baked.texels); // CPU copy: sampleShoreDepth (buoyancy) reads this
			m_shoreCenter = baked.center;
			m_shoreActiveRange = baked.ranges.x;
			m_shoreValid = true;
		}
		if (!active && m_shoreValid)
		{
			renderer.clearOceanShoreMap(); // the shaders fall back to the fog terrain cascades / open ocean
			m_shoreValid = false;
		}
	}

	void OceanRenderer::update(Renderer& renderer, const Camera& camera, std::shared_ptr<const ITerrainSampler> terrain)
	{
		if (m_enabled)
			updateShoreMap(renderer, camera, terrain);

		// Push the spectrum/shading params every frame; `enabled` also gates the GPU FFT simulation.
		OceanParams params;
		params.enabled = m_enabled;
		params.windDirection = glm::vec2(std::cos(m_windAngle), std::sin(m_windAngle));
		params.windSpeed = m_windSpeed;
		params.fetchKm = m_fetchKm;
		params.depth = m_depth;
		params.amplitude = m_amplitude;
		params.choppiness = m_choppiness;
		params.normalStrength = m_normalStrength;
		params.cascadeSizes = m_cascadeSizes;
		params.seaLevel = m_seaLevel;
		params.detailBias = m_detailBias;
		params.absorption = m_absorption;
		params.scatterColor = m_scatterColor;
		params.scatterStrength = m_scatterStrength;
		params.roughness = m_roughness;
		params.glintSharpness = m_glintSharpness;
		params.glintFilter = m_glintFilter;
		params.sssStrength = m_sssStrength;
		params.sssPower = m_sssPower;
		params.hitLighting = m_hitLighting;
		params.foamColor = m_foamColor;
		params.foamBias = m_foamBias;
		params.foamBreakAccel = m_foamBreakAccel;
		params.foamSoftness = m_foamSoftness;
		params.foamDecay = m_foamDecay;
		params.foamSpread = m_foamSpread;
		params.foamBoost = m_foamBoost;
		params.turbidity = m_turbidity;
		params.shoalScale = m_shoalScale;
		params.shoreFoamDepth = m_shoreFoamDepth;
		params.cullMargin = m_cullMargin;
		renderer.setOceanParams(params);

		if (!m_enabled)
		{
			renderer.setOceanWaveTrough(0.0f); // no waves: the underwater-fog boundary sits at the calm level
			return;
		}

		if (m_gridDirty || !m_node.isValid())
			rebuildGrid();
		if (!m_node.isValid())
			return;

		// Follow the camera, snapped to a multiple of the ring lattices so vertices re-land on the exact
		// same world positions (a clipmap's whole point: each world point keeps its sample position and
		// ring-fixed mip, so waves are rock-stable under camera motion). 8*cell aligns rings 0-2 perfectly;
		// coarser rings shift sub-texel, which is invisible against their band-limited content.
		const float snap = 8.0f * glm::max(m_ringCell, 0.01f);
		const float px = std::floor(camera.position.x / snap + 0.5f) * snap;
		const float pz = std::floor(camera.position.z / snap + 0.5f) * snap;
		m_node.setTransform(Transform(glm::vec3(px, m_seaLevel, pz), 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));

		renderer.renderNode(m_node, RendererVKLayout::PASS_MAIN);

		// Refresh the CPU copy of the GPU displacement readback inside this frame's fence-safe window
		// (the slot's buffer is stable between beginFrame and present, and physics updates BEFORE
		// beginFrame — so buoyancy must query an owned copy, not the live buffer).
		uint32 tileRes = 0;
		const std::span<const uint16> tile = renderer.getOceanDisplacementReadback(tileRes);
		m_dispTile.assign(tile.begin(), tile.end());
		m_dispTileRes = tileRes;

		estimateWaveTrough();
		renderer.setOceanWaveTrough(m_waveTrough); // sinks the underwater-fog boundary below live troughs
	}

	// FP16 -> FP32 (readback texels are RGBA16F). Fabian Giesen's half_to_float_fast: rebias the
	// exponent in float position; the multiply trick renormalizes subnormals.
	static float halfToFloat(uint16 h)
	{
		const uint32 shiftedExp = 0x7C00u << 13;
		uint32 o = (uint32)(h & 0x7FFFu) << 13;
		const uint32 exp = shiftedExp & o;
		o += (127u - 15u) << 23;
		if (exp == shiftedExp)
			o += (128u - 16u) << 23; // inf/nan stays inf/nan
		else if (exp == 0)
		{
			o += 1u << 23;
			o = std::bit_cast<uint32>(std::bit_cast<float>(o) - std::bit_cast<float>((113u << 23)));
		}
		o |= (uint32)(h & 0x8000u) << 16;
		return std::bit_cast<float>(o);
	}

	// Water depth (m) at (x, z) from the CPU shore height-map copy (depth = live sea level - height); the
	// open-ocean depth outside the baked region or with no shore data. Mirrors oceanSampleShoreDepth in
	// ocean_wave.inc.glsl minus the fog-cascade fallback — buoyancy queries only matter near the camera,
	// well inside the shore map.
	// (water depth, water surface level) at (x, z) from the CPU copy of the baked shore map — the CPU
	// mirror of the shaders' oceanSampleShoreData. Outside the map: open-ocean depth at sea level.
	glm::vec2 OceanRenderer::sampleShoreData(float x, float z) const
	{
		if (!m_shoreValid || m_shoreHeights.empty() || m_shoreActiveRange <= 1.0f)
			return glm::vec2(m_depth, m_seaLevel);
		constexpr uint32 RES = RendererVKLayout::OCEAN_SHORE_RES;
		const float u = (x - m_shoreCenter.x) / m_shoreActiveRange + 0.5f;
		const float v = (z - m_shoreCenter.y) / m_shoreActiveRange + 0.5f;
		if (u <= 0.0f || u >= 1.0f || v <= 0.0f || v >= 1.0f)
			return glm::vec2(m_depth, m_seaLevel);
		const float tx = glm::clamp(u * (float)RES - 0.5f, 0.0f, (float)RES - 1.001f);
		const float tz = glm::clamp(v * (float)RES - 0.5f, 0.0f, (float)RES - 1.001f);
		const uint32 x0 = (uint32)tx, z0 = (uint32)tz;
		const float fx = tx - (float)x0, fz = tz - (float)z0;
		const auto at = [&](uint32 i, uint32 j) { // (terrain height, water level) of the RGBA texel
			const float* t = &m_shoreHeights[((size_t)j * RES + i) * 4];
			return glm::vec2(t[0], t[1]);
		};
		const glm::vec2 hw = glm::mix(glm::mix(at(x0, z0), at(x0 + 1, z0), fx),
			glm::mix(at(x0, z0 + 1), at(x0 + 1, z0 + 1), fx), fz);
		return glm::vec2(hw.y - hw.x, hw.y);
	}

	float OceanRenderer::sampleShoreDepth(float x, float z) const
	{
		return sampleShoreData(x, z).x;
	}

	// Deepest-possible current wave trough (m below the calm level) from the readback: the sum of each
	// cascade's layer minimum bounds any combined trough (cascades add; their minima rarely coincide, so
	// this is conservative — right for hiding fog under the surface). The minimum over the WHOLE tiling
	// patch is a sea-state statistic, near-stationary frame to frame, so re-scan sparsely.
	void OceanRenderer::estimateWaveTrough()
	{
		if (m_waveTroughCooldown-- > 0 || m_dispTileRes == 0)
			return;
		m_waveTroughCooldown = 15;
		float troughSum = 0.0f;
		for (uint32 c = 0; c < RendererVKLayout::OCEAN_CASCADES; ++c)
		{
			const size_t n = (size_t)m_dispTileRes * m_dispTileRes;
			const uint16* layer = m_dispTile.data() + (size_t)c * n * 4;
			float minH = 0.0f;
			for (size_t i = 0; i < n; ++i)
				minH = glm::min(minH, halfToFloat(layer[i * 4 + 1])); // texel.y = height displacement
			troughSum -= minH;
		}
		m_waveTrough = troughSum;
	}

	// Shoal-faded sum of all cascades' displacement at an undisplaced world XZ, bilinear-wrapped over
	// the readback tile — the CPU mirror of oceanSampleDisplacement (at the readback mip's band limit).
	glm::vec3 OceanRenderer::sampleDisplacement(glm::vec2 worldXZ, float depth) const
	{
		const uint32 res = m_dispTileRes;
		const float shoal = glm::max(m_shoalScale, 0.0f);
		glm::vec3 disp(0.0f);
		for (uint32 c = 0; c < RendererVKLayout::OCEAN_CASCADES; ++c)
		{
			const float L = glm::max(m_cascadeSizes[c], 1.0f);
			const float fade = glm::smoothstep(0.0f, glm::max(shoal * L, 0.01f), depth);
			if (fade <= 0.0f)
				continue;
			// Bilinear with wrap (the FFT patch tiles); texel centers at integer + 0.5.
			const glm::vec2 t = glm::fract(glm::vec2(worldXZ.x, worldXZ.y) / L) * (float)res - 0.5f;
			const int x0 = (int)std::floor(t.x), z0 = (int)std::floor(t.y);
			const float fx = t.x - (float)x0, fz = t.y - (float)z0;
			const size_t layer = (size_t)c * res * res;
			const auto fetch = [&](int i, int j) {
				const uint32 xi = (uint32)(i + (int)res) % res, zi = (uint32)(j + (int)res) % res;
				const uint16* texel = &m_dispTile[(layer + (size_t)zi * res + xi) * 4];
				return glm::vec3(halfToFloat(texel[0]), halfToFloat(texel[1]), halfToFloat(texel[2])); // Dx, h, Dz
			};
			const glm::vec3 s = glm::mix(glm::mix(fetch(x0, z0), fetch(x0 + 1, z0), fx),
				glm::mix(fetch(x0, z0 + 1), fetch(x0 + 1, z0 + 1), fx), fz);
			disp += glm::vec3(s.x * m_choppiness, s.y, s.z * m_choppiness) * fade;
		}
		return disp;
	}

	float OceanRenderer::sampleWaterHeight(float x, float z) const
	{
		if (!m_enabled || m_dispTile.empty() || m_dispTileRes == 0)
			return -FLT_MAX;
		const glm::vec2 depthLevel = sampleShoreData(x, z);
		const float depth = depthLevel.x;
		if (depth <= 0.05f)
			return -FLT_MAX; // terrain at/above the local water level: no water here
		// The maps store where the UNDISPLACED grid point ENDS UP; a fixed world column needs the
		// inverse. A few fixed-point iterations: find p whose displaced position lands on (x, z).
		const glm::vec2 query(x, z);
		glm::vec2 p = query;
		for (int i = 0; i < 2; ++i)
		{
			const glm::vec3 d = sampleDisplacement(p, depth);
			p = query - glm::vec2(d.x, d.z);
		}
		float h = sampleDisplacement(p, depth).y;
		h = glm::max(h, 0.05f - depth); // seabed clamp, mirrors ocean_wave.inc.glsl
		return depthLevel.y + h; // waves ride the LOCAL water surface (lakes sit above sea level)
	}
}
