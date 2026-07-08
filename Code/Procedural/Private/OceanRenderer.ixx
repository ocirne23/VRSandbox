export module Procedural:OceanRenderer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

export namespace Procedural
{
	// Render-only procedural ocean, the CPU side of the FFT/Tessendorf water: maintains one camera-following
	// grid with geometric (clipmap-style) radial spacing — cell size doubles ~`lodLevels` times from the
	// camera out to the horizon, so the foreground is finely tessellated while one connected (crack-free)
	// mesh still reaches the horizon. Everything else runs on the GPU: OceanSimulationPipeline simulates the
	// wave spectrum + IFFT into displacement/gradient maps each frame, the Ocean pipeline variant displaces
	// this grid by them and shades the surface (RT refraction, Beer-Lambert). The CPU only ever positions
	// one RenderNode; the wave field is world-space, so snapping the grid under the camera keeps the water
	// world-anchored while it follows.
	//
	// All spectrum/shading parameters are Tweak-backed and pushed to the renderer each frame via
	// Renderer::setOceanParams (fully live: the spectrum is re-evaluated on the GPU every frame); the grid
	// geometry rebuilds only when its extent/resolution changes.
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
		int   m_lodLevels = 8;         // geometric detail shells camera->horizon (higher = finer near-camera cells)
		// Node XZ snap step (m). Default 0 = follow continuously: on a radially GRADED grid the vertices
		// can never land back on a fixed world lattice anyway, so snapping only quantizes the (otherwise
		// smooth) lattice motion into visible discrete jumps.
		float m_snap = 0.0f;

		// --- Spectrum (TMA/JONSWAP + finite-depth dispersion) + shading; all live via setOceanParams ---
		float m_windSpeed = 10.5f;     // U10 (m/s): the main sea-state knob
		float m_fetchKm = 300.0f;      // wind fetch (km)
		float m_depth = 100.0f;        // ocean depth (m): finite-depth dispersion + TMA attenuation
		float m_windAngle = 0.45f;     // radians (XZ heading of the dominant swell)
		float m_amplitude = 1.0f;      // artistic scale on the spectrum (1 = physical)
		float m_choppiness = 1.1f;     // horizontal displacement lambda
		float m_normalStrength = 1.0f;
		glm::vec3 m_cascadeSizes = glm::vec3(384.0f, 47.0f, 6.3f); // FFT patch sizes (m)

		glm::vec3 m_absorption = glm::vec3(0.42f, 0.085f, 0.04f);  // Beer-Lambert extinction (1/m)
		glm::vec3 m_scatterColor = glm::vec3(0.012f, 0.08f, 0.085f);
		float m_scatterStrength = 1.0f;
		float m_roughness = 0.07f;
		bool  m_hitLighting = false; // grid lights at refraction/reflection ray hits (pipeline reload on toggle)
		// Foam & turbulence: one instant-foam response draws the crest foam AND injects the accumulated
		// turbulence field, which in turn relaxes the fold threshold (aged foam along live geometry) and
		// makes the wake milky/rough.
		glm::vec3 m_foamColor = glm::vec3(0.88f, 0.92f, 0.94f);
		float m_foamBias = 0.6f;     // fold threshold (Jacobian below this foams)
		float m_foamBreakAccel = 0.25f; // breaking threshold (downward crest accel, g units)
		float m_foamSoftness = 0.5f; // edge width of both thresholds
		float m_foamDecay = 0.985f;  // turbulence retention per frame (wake persistence)
		float m_foamSpread = 1.2f;   // turbulence diffusion per frame (wake spreads as it lives)
		float m_foamBoost = 0.6f;    // turbulence -> fold-threshold relaxation (aged-foam amount)
		float m_turbidity = 0.4f;    // entrained bubbles: milky brightening + roughness of the wake

		bool m_gridDirty = true;

		// container declared first -> destroyed AFTER the node it owns the meshes for
		std::unique_ptr<ObjectContainer> m_container;
		RenderNode m_node;
	};
}
