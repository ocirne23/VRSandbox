export module Procedural:OceanRenderer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

export namespace Procedural
{
	// Render-only procedural ocean. Maintains one camera-following grid with geometric (clipmap-style) radial
	// spacing — cell size doubles ~`lodLevels` times from the camera out to the horizon, so the foreground is
	// finely tessellated while one connected (crack-free) mesh still reaches the horizon. Its vertices are
	// Gerstner-displaced into animated waves entirely on the GPU by the Ocean pipeline variant, so the CPU
	// only ever positions one RenderNode. The wave field is world-space and time-animated, so snapping the
	// grid under the camera keeps the water world-anchored while it follows.
	//
	// All wave/shading parameters are Tweak-backed and pushed to the renderer's UBO each frame via
	// Renderer::setOceanParams; the grid geometry rebuilds only when its extent/resolution changes.
	class OceanRenderer
	{
	public:
		OceanRenderer() = default;
		~OceanRenderer();
		OceanRenderer(const OceanRenderer&) = delete;
		OceanRenderer& operator=(const OceanRenderer&) = delete;

		void initialize();                                     // registers Tweaks
		void update(Renderer& renderer, const Camera& camera); // per-frame: push params + render (after beginFrame)

	private:
		void rebuildGrid();

		// --- Geometry config (a change rebuilds the grid mesh) ---
		bool  m_enabled = false;
		float m_seaLevel = 0.0f;
		float m_extent = 4000.0f;      // half-size the grid reaches toward the horizon (m)
		int   m_resolution = 256;      // grid vertices per axis
		int   m_lodLevels = 7;         // geometric detail shells camera->horizon (higher = finer near-camera cells)
		float m_snap = 4.0f;           // node XZ snap step (m); reduces vertex swim as the grid follows

		// --- Wave spectrum + shading (fed to Renderer::setOceanParams; all live) ---
		float m_amplitude = 0.55f;
		float m_choppiness = 0.85f;
		float m_wavelength = 62.0f;
		float m_windAngle = 0.45f;     // radians (XZ heading of the dominant swell)
		float m_speed = 1.0f;
		float m_detailScale = 1.0f;
		float m_roughness = 0.045f;
		glm::vec3 m_deepColor = glm::vec3(0.0040f, 0.0160f, 0.0290f);
		glm::vec3 m_scatterColor = glm::vec3(0.020f, 0.14f, 0.13f);
		float m_scatterStrength = 1.0f;
		glm::vec3 m_foamColor = glm::vec3(0.85f, 0.90f, 0.92f);
		float m_foamCoverage = 0.42f;

		bool m_gridDirty = true;

		// container declared first -> destroyed AFTER the node it owns the meshes for
		std::unique_ptr<ObjectContainer> m_container;
		RenderNode m_node;
	};
}
