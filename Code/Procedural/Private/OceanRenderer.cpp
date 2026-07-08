module Procedural;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;
import Core.Tweaks;

import RendererVK;
import File;

import :OceanRenderer;

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

	void OceanRenderer::update(Renderer& renderer, const Camera& camera)
	{
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
		params.hitLighting = m_hitLighting;
		params.foamColor = m_foamColor;
		params.foamBias = m_foamBias;
		params.foamBreakAccel = m_foamBreakAccel;
		params.foamSoftness = m_foamSoftness;
		params.foamDecay = m_foamDecay;
		params.foamSpread = m_foamSpread;
		params.foamBoost = m_foamBoost;
		params.turbidity = m_turbidity;
		renderer.setOceanParams(params);

		if (!m_enabled)
			return;

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
	}
}
