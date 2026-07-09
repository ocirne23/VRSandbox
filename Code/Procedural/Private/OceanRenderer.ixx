export module Procedural:OceanRenderer;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

import :Climate;

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
		// Per-frame: push params + render (after beginFrame). terrain = the streamer's live height field
		// (TerrainStreamer::activeClimateMaps(), nullptr = no terrain): with it the ocean bakes a coarse
		// water-depth map around the camera on a worker thread — shallow water fades the waves (fake
		// shoaling), the waterline grows a surf band, and waves never poke through land.
		void update(Renderer& renderer, const Camera& camera, std::shared_ptr<const ClimateMaps> terrain = nullptr);

		// Water surface world Y at (x, z), CPU-evaluated from the GPU displacement readback (mirrors the
		// vertex shader's cascade sum, shoaling fade and seabed clamp; ~2 frames of latency — invisible
		// for physics). Returns -FLT_MAX where there is no water: ocean disabled, readback not primed,
		// or terrain at/above sea level per the shore bake. This is the buoyancy height field the App
		// wires into PhysicsWorld::setWaterSurface; keys 8/9's cubes bob in the swell through it.
		float sampleWaterHeight(float x, float z) const;

	private:
		void rebuildGrid();
		void updateShoreMap(Renderer& renderer, const Camera& camera, const std::shared_ptr<const ClimateMaps>& terrain);
		float sampleShoreDepth(float x, float z) const;             // CPU copy of the baked shore map
		glm::vec3 sampleDisplacement(glm::vec2 worldXZ, float depth) const; // shoal-faded cascade sum from the readback tile

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

		glm::vec3 m_absorption = glm::vec3(0.897f, 0.082f, 0.15f);  // Beer-Lambert extinction (1/m)
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

		// --- Shore interaction (terrain water-depth bake; see updateShoreMap) ---
		bool  m_shoreEnabled = true;
		float m_shoreRange = 1024.0f;   // world size (m) the baked map covers, centered on the camera
		float m_shoalScale = 0.05f;     // waves fade below depth = scale * cascade patch size
		float m_shoreFoamDepth = 1.5f;  // surf band: water-column height (m) that churns white; 0 = off

		// Bake state: one async bake at a time (std::async); the result ships to the GPU when ready and
		// the previous map stays active meanwhile. A bake pins its ClimateMaps via the captured shared_ptr.
		std::future<std::vector<float>> m_bakeFuture;
		glm::vec2 m_bakePendingCenter = glm::vec2(0.0f); // inputs of the IN-FLIGHT bake
		float m_bakePendingRange = 0.0f;
		float m_bakePendingSeaLevel = 0.0f;
		const ClimateMaps* m_bakePendingMaps = nullptr;  // identity only (never dereferenced)
		glm::vec2 m_shoreCenter = glm::vec2(0.0f);       // inputs of the ACTIVE (uploaded) map
		float m_shoreActiveRange = 0.0f;
		float m_shoreBakedSeaLevel = 0.0f;
		const ClimateMaps* m_shoreBakedMaps = nullptr;   // identity only (never dereferenced)
		bool  m_shoreValid = false;
		std::vector<float> m_shoreDepths;                // CPU copy of the active map (buoyancy queries)

		// CPU copy of the GPU displacement readback tile, refreshed every update() inside the frame's
		// fence-safe window — sampleWaterHeight can then run at ANY point in the frame (physics updates
		// before beginFrame) without racing the GPU rewriting the slot's readback buffer.
		std::vector<uint16> m_dispTile;                  // RGBA16F texels, res^2 per cascade
		uint32 m_dispTileRes = 0;

		bool m_gridDirty = true;

		// container declared first -> destroyed AFTER the node it owns the meshes for
		std::unique_ptr<ObjectContainer> m_container;
		RenderNode m_node;
	};
}
