export module Procedural:OceanRenderer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

export namespace Procedural
{
	// Render-only procedural ocean, the CPU side of the FFT/Tessendorf water: maintains a camera-following
	// GEOMETRY CLIPMAP — concentric square rings, each with a FIXED world-space cell size that doubles per
	// ring. Because a ring's cell size is constant, every world position inside it samples the displacement
	// maps at a FIXED mip regardless of camera distance — wave shapes no longer morph with camera motion
	// (the failure mode of the previous radially-graded grid, whose per-vertex mip followed distance). Ring
	// transitions use a CDLOD-style vertex morph baked per vertex (texcoord = ring cell size + morph
	// weight): over each ring's outer band, odd vertices collapse onto the next ring's coarser lattice and
	// the sampled mip blends +1, so the boundary matches the next ring exactly — seamless by construction.
	// The whole clipmap is ONE mesh on one node, snapped to a lattice multiple so vertices re-land on the
	// same world positions as the camera moves.
	//
	// Everything else runs on the GPU: OceanSimulationPipeline simulates the wave spectrum + IFFT into
	// displacement/gradient maps each frame, the Ocean pipeline variant displaces this mesh by them and
	// shades the surface (RT refraction, Beer-Lambert). All spectrum/shading parameters are Tweak-backed
	// and pushed via Renderer::setOceanParams (fully live); the mesh rebuilds only when ring params change.
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

		// --- Clipmap geometry config (a change rebuilds the mesh) ---
		bool  m_enabled = false;
		float m_seaLevel = 0.0f;
		float m_ringCell = 0.125f;     // ring 0 cell size (m); doubles per ring. Reach = ringCell*res/2*2^(rings-1)
		int   m_ringRes = 512;         // cells per axis per ring (ring 0 is a full grid, outer rings are annuli)
		int   m_rings = 7;             // ring count (defaults: 16m fine region, ~1km reach)
		// Bias on the ring-matched displacement mip (negative = sample finer than the ring's Nyquist:
		// slightly more detail, some sampling shimmer while moving). With fixed-cell rings 0 should be fine.
		float m_detailBias = 0.0f;

		// --- Spectrum (TMA/JONSWAP + finite-depth dispersion) + shading; all live via setOceanParams ---
		float m_windSpeed = 7.5f;     // U10 (m/s): the main sea-state knob
		float m_fetchKm = 300.0f;      // wind fetch (km)
		float m_depth = 100.0f;        // ocean depth (m): finite-depth dispersion + TMA attenuation
		float m_windAngle = 0.45f;     // radians (XZ heading of the dominant swell)
		float m_amplitude = 1.0f;      // artistic scale on the spectrum (1 = physical)
		float m_choppiness = 0.33f;     // horizontal displacement lambda
		float m_normalStrength = 1.0f;
		glm::vec3 m_cascadeSizes = glm::vec3(384.0f, 47.0f, 6.3f); // FFT patch sizes (m)

		glm::vec3 m_absorption = glm::vec3(0.42f, 0.085f, 0.04f);  // Beer-Lambert extinction (1/m)
		glm::vec3 m_scatterColor = glm::vec3(0.047f, 0.1f, 0.15f);
		float m_scatterStrength = 1.0f;
		float m_roughness = 0.07f;
		bool  m_hitLighting = false; // grid lights at refraction/reflection ray hits (pipeline reload on toggle)
		// Foam & turbulence: one instant-foam response draws the crest foam AND injects the accumulated
		// turbulence field, which in turn relaxes the fold threshold (aged foam along live geometry) and
		// makes the wake milky/rough.
		glm::vec3 m_foamColor = glm::vec3(0.88f, 0.92f, 0.94f);
		float m_foamBias = 0.61f;     // fold threshold (Jacobian below this foams)
		float m_foamBreakAccel = 0.25f; // breaking threshold (downward crest accel, g units)
		float m_foamSoftness = 0.75f; // edge width of both thresholds
		float m_foamDecay = 0.999f;  // turbulence retention per frame (wake persistence)
		float m_foamSpread = 1.2f;   // turbulence diffusion per frame (wake spreads as it lives)
		float m_foamBoost = 0.67f;    // turbulence -> fold-threshold relaxation (aged-foam amount)
		float m_turbidity = 0.0f;    // entrained bubbles: milky brightening + roughness of the wake

		bool m_gridDirty = true;

		// container declared first -> destroyed AFTER the node it owns the meshes for
		std::unique_ptr<ObjectContainer> m_container;
		RenderNode m_node;
	};
}
