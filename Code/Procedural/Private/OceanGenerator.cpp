module Procedural;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;
import Core.Tweaks;
import Core.Time;

import RendererVK;
import File;

import :OceanGenerator;
import :TerrainSampler;

namespace Procedural
{
	OceanGenerator::~OceanGenerator()
	{
		// Free the RenderNode before its ObjectContainer, while the renderer/device are still alive.
		m_node = RenderNode();
		m_container.reset();
	}

	void OceanGenerator::initialize()
	{
		auto gridDirty = [this]() { m_gridDirty = true; };

		Tweak::boolean("Ocean", "Enabled", &m_enabled);
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
		// Flow -> wind steering: near a coast the SIMULATION wind turns toward the baked flow directions
		// (waves roll in toward the local shore); away from any shore it returns to the wind angle above.
		Tweak::boolean("Ocean/Waves", "Flow steers wind", &m_windSteerEnabled);
		Tweak::floatVar("Ocean/Waves", "Steer rate (deg/s)", &m_windSteerRate, 0.0f, 90.0f, 0.5f);
		Tweak::floatVar("Ocean/Waves", "Steer range (m)", &m_windSteerRange, 0.0f, 2000.0f, 10.0f);
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
		Tweak::floatVar("Ocean/Shore", "Shoal depth scale", &m_shoalScale, 0.0f, 0.1f, 0.001f);
		Tweak::floatVar("Ocean/Shore", "Shore foam depth (m)", &m_shoreFoamDepth, 0.0f, 8.0f, 0.05f);
		Tweak::floatVar("Ocean/Shore", "Shore foam max", &m_shoreFoamMax, 0.0f, 1.0f, 0.01f);
		Tweak::floatVar("Ocean/Shore", "Swash amplitude", &m_swashAmp, 0.0f, 2.0f, 0.01f);
		Tweak::floatVar("Ocean/Shore", "Swash drawdown (m)", &m_swashDrawdown, 0.00f, 2.0f, 0.01f);
		// A wave cannot stand taller than the water it is in. The waterline clamp already keeps the TROUGH
		// off the seabed; this is the other half, and without it nothing bounded the crest except the shoal
		// fade — a fixed depth/(scale*patch) ramp, not a limit relative to the water column. A crest allowed
		// to stand a metre high in two metres of water gets carried over the beach by the clipmap triangle
		// that bridges it to the next vertex on land. Real waves break near H/d ~ 0.78 (crest ~ 0.39*d), so
		// lower it to break them earlier and further out. 0 = unbounded (the old behaviour).
		Tweak::floatVar("Ocean/Shore", "Crest limit (x depth)", &m_crestLimit, 0.0f, 2.0f, 0.01f);
		// The other half: how far above the seabed the TROUGH is held. The clamp measures against the baked
		// depth map, but you see the LOD'd terrain mesh, and the two disagree by decimetres — so the old
		// hard-coded 5 cm let a trough that clears the map's seabed sink under the real ground, which then
		// pokes through the surface. This is the margin for that error, so size it to the disagreement, not
		// to the waves. Tapered in with depth, so the waterline does not lift and retreat.
		Tweak::floatVar("Ocean/Shore", "Trough margin (m)", &m_troughMargin, 0.0f, 1.0f, 0.01f);
		Tweak::floatVar("Ocean/Shore", "Shore foam bias", &m_shoreFoamBias, -1.0f, 1.0f, 0.01f);
		Tweak::floatVar("Ocean/Shore", "Swash backflow", &m_swashFlow, 0.0f, 3.0f, 0.01f);
		// Land cull: clipmap triangles buried deeper than this under the local water level (over their
		// whole footprint) are discarded in the vertex shaders — no displacement sampling, no raster.
		Tweak::floatVar("Ocean/Shore", "Cull margin (m)", &m_cullMargin, 0.0f, 4.0f, 0.05f);
	}

	void OceanGenerator::rebuildGrid()
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
	void OceanGenerator::updateShoreMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ITerrainSampler>& terrain,
	                                   const WaterReach* reach, const FlowField* flow)
	{
		const bool active = m_shoreEnabled && terrain != nullptr;
		HeightMapBaker::Baked baked;
		if (m_shoreBaker.update(baked, active, terrain, glm::vec2(camera.position.x, camera.position.z),
			glm::vec2(glm::max(m_shoreRange, 256.0f), 0.0f), RendererVKLayout::OCEAN_SHORE_RES, 1, 4,
			true, reach, flow)) // shore layout: h, water, flow, spare
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

	// Baked flow -> simulation wind. The FFT field travels along the wind its SPECTRUM was built with, and
	// the spectrum regenerates every frame — so turning that wind is the one continuous way to make the
	// whole sea (every cascade, chop, foam) genuinely travel toward the local shore. The per-pixel
	// alternative — rotating each texel's sample position by its own flow angle (oceanFlowRotation) — is
	// disabled in the shader: the rotation pivots on the world origin, so the sea creased along every
	// 8-bit angle contour, worse with distance.
	// Here the baked shore directions around the camera vote (ocean texels carrying a direction; land and
	// undirected open sea abstain) and the sim wind slews toward their mean at a bounded rate, morphing
	// the spectrum smoothly through the turn. The baked field itself eases back to the base wind offshore
	// (FlowField::oceanFade), so far-from-land votes already agree with the tweak angle and leaving the
	// coast hands back to it by construction.
	float OceanGenerator::steeredWindAngle(const Camera& camera)
	{
		if (!m_windSteerEnabled || m_windSteerRate <= 0.0f)
		{
			m_windSteerSynced = false; // re-adopt the base wind when steering comes back on
			return m_windAngle;
		}
		if (!m_windSteerSynced)
		{
			m_steeredWindAngle = m_windAngle;
			m_windSteerSynced = true;
		}
		glm::vec2 sum(0.0f);
		if (m_shoreValid && m_shoreActiveRange > 0.0f && m_windSteerRange > 0.0f)
		{
			const int32 res = (int32)RendererVKLayout::OCEAN_SHORE_RES;
			const float texel = m_shoreActiveRange / (float)res;
			const int32 radius = (int32)(m_windSteerRange / texel);
			const int32 step = glm::max(radius / 16, 1); // <= 33x33 taps of the CPU shore copy
			const glm::vec2 rel = glm::vec2(camera.position.x, camera.position.z) - m_shoreCenter;
			const int32 cx = (int32)std::floor(rel.x / texel) + res / 2;
			const int32 cy = (int32)std::floor(rel.y / texel) + res / 2;
			for (int32 y = glm::max(cy - radius, 0); y <= glm::min(cy + radius, res - 1); y += step)
				for (int32 x = glm::max(cx - radius, 0); x <= glm::min(cx + radius, res - 1); x += step)
				{
					const float* t = &m_shoreHeights[((size_t)y * res + x) * 4];
					const uint32 enc = (uint32)(t[2] + 0.5f);
					if (enc == 0u || t[0] >= t[1]) // undirected or dry ground: abstains
						continue;
					const float a = (float)(enc - 1u) * (6.283185307f / 254.0f);
					sum += glm::vec2(std::cos(a), std::sin(a));
				}
		}
		// Needs a net vote worth at least one texel: a lone sliver of coast at the range's edge may turn
		// the sea, a wash of mutually cancelling directions may not. Votes are the direction the water
		// should TRAVEL; the sim's dominant waves travel AGAINST its wind vector (see swellTravelAngle),
		// so the wind target points the opposite way — offshore votes (faded to the travel heading) then
		// negate right back to the base tweak.
		const float target = glm::dot(sum, sum) > 1.0f ? std::atan2(-sum.y, -sum.x) : m_windAngle;
		float d = target - m_steeredWindAngle;
		d -= std::floor(d * (1.0f / 6.283185307f) + 0.5f) * 6.283185307f; // shortest arc
		const float maxStep = glm::radians(m_windSteerRate) * (float)glm::min(Globals::time.getDeltaSec(), 0.1);
		m_steeredWindAngle += glm::clamp(d, -maxStep, maxStep);
		return m_steeredWindAngle;
	}

	void OceanGenerator::update(Renderer& renderer, const Camera& camera, std::shared_ptr<const ITerrainSampler> terrain,
	                           const WaterReach* reach, float seaLevel, const FlowField* flow)
	{
		// ONE sea level, owned by the terrain (see the header). Adopted here every frame rather than
		// tweaked separately: this used to be its own slider, and the two silently forked — the water plane
		// moved while the terrain kept reporting its water at the old datum, which does not merely look
		// wrong. The swash gate fades on |baked water level - sea level|, so a metre of disagreement turned
		// the swash off planet-wide.
		m_seaLevel = seaLevel;
		if (m_enabled)
			updateShoreMap(renderer, camera, terrain, reach, flow);

		// Push the spectrum/shading params every frame; `enabled` also gates the GPU FFT simulation.
		const float windAngle = steeredWindAngle(camera); // base wind, turned toward the local shore flow
		OceanParams params;
		params.enabled = m_enabled;
		params.windDirection = glm::vec2(std::cos(windAngle), std::sin(windAngle));
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
		params.shoreFoamMax = m_shoreFoamMax;
		params.swashAmp = m_swashAmp;
		params.swashDrawdown = m_swashDrawdown;
		params.crestDepthLimit = m_crestLimit;
		params.troughMargin = m_troughMargin;
		params.shoreFoamBias = m_shoreFoamBias;
		params.swashFlow = m_swashFlow;
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

	// (water depth, water surface level) at (x, z) from the CPU copy of the baked shore map — the CPU
	// mirror of the shaders' oceanSampleShoreData (depth = local water level - terrain height). Outside
	// the map: open-ocean depth at sea level. Two knowing omissions vs the shader: the fog-cascade
	// fallback and the edge blend band — buoyancy queries only matter near the camera, well inside the
	// shore map's full-weight interior.
	glm::vec2 OceanGenerator::sampleShoreData(float x, float z) const
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

	// Deepest-possible current wave trough (m below the calm level) from the readback: the sum of each
	// cascade's layer minimum bounds any combined trough (cascades add; their minima rarely coincide, so
	// this is conservative — right for hiding fog under the surface). The minimum over the WHOLE tiling
	// patch is a sea-state statistic, near-stationary frame to frame, so re-scan sparsely.
	void OceanGenerator::estimateWaveTrough()
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

	// Swash run-up reach (m). MIRRORS Renderer.cpp's UBO packing of u_oceanParams7.w — the conservative
	// max run-up height derived from the wave-trough estimate this class itself publishes. It sizes the
	// on-land band the shaders draw the tongue in, so it is also the band buoyancy must find water in.
	float OceanGenerator::swashReach() const
	{
		return glm::clamp(m_swashAmp, 0.0f, 4.0f) * (m_waveTrough + 0.25f);
	}

	// CPU mirror of oceanSwashWeight (ocean_wave.inc.glsl): how much of the RAW un-shoaled wave field
	// rides the surface at this water depth (negative = land height above the local level).
	float OceanGenerator::swashWeight(float depth, float waterLevel) const
	{
		const float amp = glm::clamp(m_swashAmp, 0.0f, 4.0f);
		if (amp <= 0.0f)
			return 0.0f;
		const float seaFade = 1.0f - glm::smoothstep(0.05f, 1.0f, std::fabs(waterLevel - m_seaLevel));
		if (seaFade <= 0.0f)
			return 0.0f; // landlocked water (lakes at altitude): no swell reaches it
		const float reach = glm::max(swashReach(), 0.01f);
		const float landFade = glm::clamp(1.0f + glm::min(depth, 0.0f) / reach, 0.0f, 1.0f);
		const float fadeIn = 1.0f - glm::smoothstep(0.0f,
			glm::max(2.0f * reach, glm::max(m_shoalScale, 0.0f) * glm::max(m_cascadeSizes.y, 1.0f)), depth);
		return amp * seaFade * landFade * fadeIn;
	}

	// CPU mirror of oceanSampleDisplacement (ocean_wave.inc.glsl) at an UNDISPLACED world XZ, bilinear-
	// wrapped over the readback tile: shoal-faded cascade sum, swash backflow, crest ceiling, the raw
	// run-up residual, then the waterline floor — same order, same clamps, y relative to the LOCAL water
	// level like the shader's. The shader is what you SEE and this is what floats on it, so any change
	// there has to land here too; the divergence is invisible until a body sinks through a drawn wave.
	// Two knowing omissions: the ring-matched vertex mip (the readback is one fixed band limit — the
	// physics surface is the same waves minus the finest detail) and the flow rotation (disabled in the
	// shader; if OCEAN_FLOW_SAMPLE_ROTATION ever comes back it must come back here too).
	glm::vec3 OceanGenerator::sampleDisplacement(glm::vec2 worldXZ) const
	{
		const glm::vec2 shoreHW = sampleShoreData(worldXZ.x, worldXZ.y); // (depth, water level)
		const float depth = shoreHW.x;
		const float reach = swashReach();
		glm::vec3 disp(0.0f);
		float sw = 0.0f;
		// Buried deeper than the run-up band: every shoal fade and the swash weight are zero, so the
		// sampling below would displace nothing — skip to the floor clamp (bit-identical, no fetches).
		if (depth > -reach)
		{
			const uint32 res = m_dispTileRes;
			// Bilinear with wrap (the FFT patch tiles); texel centers at integer + 0.5.
			const auto sampleCascade = [&](uint32 c, float L) {
				const glm::vec2 t = glm::fract(worldXZ / L) * (float)res - 0.5f;
				const int x0 = (int)std::floor(t.x), z0 = (int)std::floor(t.y);
				const float fx = t.x - (float)x0, fz = t.y - (float)z0;
				const size_t layer = (size_t)c * res * res;
				const auto fetch = [&](int i, int j) {
					const uint32 xi = (uint32)(i + (int)res) % res, zi = (uint32)(j + (int)res) % res;
					const uint16* texel = &m_dispTile[(layer + (size_t)zi * res + xi) * 4];
					return glm::vec3(halfToFloat(texel[0]), halfToFloat(texel[1]), halfToFloat(texel[2])); // Dx, h, Dz
				};
				return glm::mix(glm::mix(fetch(x0, z0), fetch(x0 + 1, z0), fx),
					glm::mix(fetch(x0, z0 + 1), fetch(x0 + 1, z0 + 1), fx), fz);
			};
			const float shoal = glm::max(m_shoalScale, 0.0f);
			float rawY = 0.0f;
			glm::vec2 rawXZ(0.0f);
			for (uint32 c = 0; c < RendererVKLayout::OCEAN_CASCADES; ++c)
			{
				const float L = glm::max(m_cascadeSizes[c], 1.0f);
				const glm::vec3 d = sampleCascade(c, L);
				disp += glm::vec3(d.x * m_choppiness, d.y, d.z * m_choppiness)
					* glm::smoothstep(0.0f, glm::max(shoal * L, 0.01f), depth); // oceanShoalFade
				rawY += d.y;
				rawXZ += glm::vec2(d.x, d.z);
			}
			sw = swashWeight(depth, shoreHW.y);
			// Backflow: the tongue slides seaward as the wave recedes, gated by its thickness above the
			// sand and soft-capped to ~the reach (a buried surface must not keep sliding).
			const float flowFade = glm::smoothstep(0.0f, 0.35f, rawY * sw + depth);
			glm::vec2 flowOff = rawXZ * (m_choppiness * glm::max(m_swashFlow, 0.0f) * sw * flowFade);
			const float flowCap = glm::clamp(0.5f * reach, 0.25f, 1.0f);
			flowOff *= flowCap / (flowCap + glm::length(flowOff));
			disp.x += flowOff.x;
			disp.z += flowOff.y;
			// Crest ceiling BEFORE the run-up add, never after: riding up the beach is the swash's job.
			const float crestLimit = glm::max(m_crestLimit, 0.0f);
			if (crestLimit > 0.0f && depth > 0.0f)
				disp.y = glm::min(disp.y, crestLimit * depth);
			disp.y += rawY * sw;
		}
		// Waterline floor: the inner-rounded smooth max, tightening under an active swash.
		constexpr float eps = 0.05f;
		const float k = glm::mix(0.2f, 0.06f, glm::clamp(sw * 4.0f, 0.0f, 1.0f));
		float floorY = eps - glm::max(depth, 2.0f * eps);
		const float troughMargin = glm::max(m_troughMargin, 0.0f);
		if (troughMargin > 0.0f && depth > 0.0f)
			floorY += troughMargin * glm::smoothstep(0.0f, 2.0f * troughMargin, depth);
		// Drawdown sinks the receding surface UNDER the sand — which is exactly what beaches a floating
		// body as the wave leaves, so buoyancy wants it as much as the depth cut does.
		if (glm::clamp(m_swashAmp, 0.0f, 4.0f) > 0.0f && depth > 0.0f && m_swashDrawdown > 0.0f)
			floorY = glm::mix(floorY, -depth - glm::max(m_swashDrawdown, eps),
				1.0f - glm::smoothstep(0.0f, glm::max(reach, 0.01f), depth));
		const float hh = glm::max(k - std::fabs(disp.y - floorY), 0.0f) / k;
		disp.y = glm::max(disp.y, floorY) - hh * hh * (k * 0.25f);
		return disp;
	}

	float OceanGenerator::sampleWaterHeight(float x, float z) const
	{
		if (!m_enabled || m_dispTile.empty() || m_dispTileRes == 0)
			return -FLT_MAX;
		// Land beyond the run-up band: the swash weight and every shoal fade are zero there, so the
		// shaders draw no live water — the same gate the displacement uses, one shore fetch instead of
		// the whole inverse. Inside the band this DOES return water above the drawn shoreline: that is
		// the tongue, and a body in it floats until the drawdown floor lets it back down onto the sand.
		if (sampleShoreData(x, z).x <= -swashReach())
			return -FLT_MAX;
		// The maps store where the UNDISPLACED grid point ENDS UP; a fixed world column needs the
		// inverse. A few fixed-point iterations: find p whose displaced position lands on (x, z).
		const glm::vec2 query(x, z);
		glm::vec2 p = query;
		for (int i = 0; i < 2; ++i)
		{
			const glm::vec3 d = sampleDisplacement(p);
			p = query - glm::vec2(d.x, d.z);
		}
		// Waves ride the LOCAL water surface, read at the SOURCE point like the clipmap vertex does
		// (its base y is the water table under the undisplaced vertex, not under where it lands).
		return sampleShoreData(p.x, p.y).y + sampleDisplacement(p).y;
	}
}
