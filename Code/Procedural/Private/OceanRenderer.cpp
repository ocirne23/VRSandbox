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
		Tweak::floatVar("Ocean", "Sea level (m)", &m_seaLevel, -500.0f, 500.0f, 0.5f);
		Tweak::floatVar("Ocean", "Extent (m)", &m_extent, 100.0f, 20000.0f, 10.0f, gridDirty);
		Tweak::intVar("Ocean", "Resolution", &m_resolution, 256, 1024, 2.0f, gridDirty);
		Tweak::intVar("Ocean", "LOD levels", &m_lodLevels, 1, 12, 1.0f, gridDirty);
		Tweak::floatVar("Ocean", "Grid snap (m)", &m_snap, 0.0f, 64.0f, 0.5f);

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

		const int   N = glm::clamp(m_resolution, 4, 1024);
		const float extent = glm::max(m_extent, 2.0f);
		const int   levels = glm::clamp(m_lodLevels, 1, 12);

		// Geometric (clipmap-style) radial spacing: cell size doubles ~`levels` times from the camera out to
		// the horizon, so the foreground gets very fine cells while one connected (crack-free) mesh still
		// reaches `extent`. More LOD levels -> smaller near-camera cells (finer detail where it counts). The
		// mapping is separable per axis, so the grid stays a simple regular quad mesh.
		const float K = float(levels) * 0.6931472f; // ln(2) * levels
		const float denom = std::exp(K) - 1.0f;
		auto grade = [extent, K, denom](float t) {
			const float at = std::fabs(t);
			const float v = (std::exp(K * at) - 1.0f) / denom; // 0..1, geometric
			return t < 0.0f ? -extent * v : extent * v;
		};

		std::vector<glm::vec3> positions((size_t)N * N);
		std::vector<glm::vec3> normals((size_t)N * N, glm::vec3(0.0f, 1.0f, 0.0f));
		for (int j = 0; j < N; ++j)
		{
			const float z = grade(-1.0f + 2.0f * (float)j / (float)(N - 1));
			for (int i = 0; i < N; ++i)
			{
				const float x = grade(-1.0f + 2.0f * (float)i / (float)(N - 1));
				positions[(size_t)j * N + i] = glm::vec3(x, 0.0f, z);
			}
		}

		std::vector<uint32> indices;
		indices.reserve((size_t)(N - 1) * (N - 1) * 6);
		for (int j = 0; j < N - 1; ++j)
		{
			for (int i = 0; i < N - 1; ++i)
			{
				const uint32 a = (uint32)(j * N + i);
				const uint32 b = (uint32)(j * N + i + 1);
				const uint32 c = (uint32)((j + 1) * N + i);
				const uint32 d = (uint32)((j + 1) * N + i + 1);
				indices.push_back(a); indices.push_back(c); indices.push_back(b);
				indices.push_back(b); indices.push_back(c); indices.push_back(d);
			}
		}

		MeshGeometryDesc geom;
		geom.positions = positions.data();
		geom.normals = normals.data();
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
		// Cell size of the graded grid as a linear function of radius (cell = A*r + B), from the same
		// grading rebuildGrid bakes: grade(t) = extent*(e^{K|t|}-1)/(e^K-1) with K = levels*ln2, so
		// d(grade)/dvertex = (2K/(N-1)) * (r + extent/(e^K-1)). The vertex shaders use it to sample the
		// displacement maps at the Nyquist-safe mip for the local vertex density — undersampling instead
		// aliases the surface, which shows as waves snapping around while the grid follows the camera.
		{
			const int   N = glm::clamp(m_resolution, 4, 1024);
			const int   levels = glm::clamp(m_lodLevels, 1, 12);
			const float extent = glm::max(m_extent, 2.0f);
			const float K = float(levels) * 0.6931472f;
			const float A = 2.0f * K / float(N - 1);
			params.gridCellA = A;
			params.gridCellB = A * extent / (std::exp(K) - 1.0f);
		}
		renderer.setOceanParams(params);

		if (!m_enabled)
			return;

		if (m_gridDirty || !m_node.isValid())
			rebuildGrid();
		if (!m_node.isValid())
			return;

		// Follow the camera, snapped to a small step so the graded vertices don't visibly swim under the
		// world-anchored wave field (the per-pixel normal is world-space, so it stays rock-steady regardless).
		const float snap = glm::max(m_snap, 0.01f);
		const float px = std::floor(camera.position.x / snap + 0.5f) * snap;
		const float pz = std::floor(camera.position.z / snap + 0.5f) * snap;
		m_node.setTransform(Transform(glm::vec3(px, m_seaLevel, pz), 1.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));

		renderer.renderNode(m_node, RendererVKLayout::PASS_MAIN);
	}
}
