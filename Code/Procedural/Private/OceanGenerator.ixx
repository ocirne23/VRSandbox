export module Procedural:OceanGenerator;

import Core;
import Core.glm;
import Core.Camera;
import Core.Transform;

import RendererVK;

import :TerrainSampler;
import :HeightMapBaker;

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
	class OceanGenerator
	{
	public:
		OceanGenerator() = default;
		~OceanGenerator();
		OceanGenerator(const OceanGenerator&) = delete;
		OceanGenerator& operator=(const OceanGenerator&) = delete;

		void initialize();                                     // registers Tweaks
		// Per-frame: push params + render (after beginFrame). terrainData = the streamer's active baked
		// terrain-data map (TerrainStreamer::activeTerrainData(), nullptr = no terrain). The GPU passes
		// read the SAME bake (the fog terrain cascades) for water depth/level — shoaling, surf, swash,
		// the land cull — and this CPU copy feeds buoyancy and wind steering, so the drawn water and the
		// simulated water agree by construction.
		// `seaLevel` is the world datum, and it comes from the TERRAIN (TerrainStreamer::seaLevel) rather
		// than being a tweak here. It cannot be two values: the terrain generator builds its heights around
		// it (V3's elevations are relative to it, which is why moving it regenerates chunks), the ocean
		// floats its surface on it, and the swash gate compares the two — so a fork between them switches
		// the swash off everywhere rather than just looking off.
		void update(Renderer& renderer, const Camera& camera,
		            std::shared_ptr<const BakedTerrainData> terrainData = nullptr, float seaLevel = 0.0f);

		// Water surface world Y at (x, z), CPU-evaluated from the GPU displacement readback (a full mirror
		// of the clipmap vertex shader: cascade sum, shoaling fade, swash run-up and the waterline
		// floor; ~2 frames of latency — invisible for physics). Returns -FLT_MAX where there is
		// no water: ocean disabled, readback not primed, or land beyond the swash run-up band per the
		// shore bake. Inside that band it returns the live tongue surface, which SINKS BELOW the terrain
		// as the wave recedes (the drawdown floor) — bodies beach themselves on their own that way, so
		// callers want a plain surface-vs-point test, not a separate dry check. This is the buoyancy
		// height field the App wires into PhysicsWorld::setWaterSurface; keys 8/9's cubes bob in the
		// swell through it.
		float sampleWaterHeight(float x, float z) const;

		// The heading the swell actually TRAVELS in open water (radians, XZ) — the terrain streamer's baked
		// flow field eases back to this offshore so the encoded directions meet the wind-driven open sea
		// without a turn. NOTE the sim's convention: the spectrum's dominant term is h0(k) e^{i(k.x + wt)},
		// which moves AGAINST the wind-direction vector, so travel = m_windAngle + pi (steeredWindAngle
		// converts back when it feeds the sim). Derived from the BASE wind tweak, deliberately not the
		// steered value: feeding that into the bake would re-bake both maps every frame the wind turns
		// (bake -> steering -> bake feedback).
		float swellTravelAngle() const { return m_windAngle + 3.14159265f; }

	private:
		// Turns the SIMULATION wind toward the baked shore flow around the camera — how the waves actually
		// travel inland at the coast. See the .cpp.
		float steeredWindAngle(const Camera& camera);
		void rebuildGrid();
		glm::vec2 sampleShoreData(float x, float z) const;          // (water depth, water level) from the terrain-data CPU copy
		float swashReach() const;                                   // run-up band height; mirrors the UBO's estimate
		float swashWeight(float depth, float waterLevel) const;     // mirrors oceanSwashWeight
		glm::vec3 sampleDisplacement(glm::vec2 worldXZ) const;      // CPU mirror of oceanSampleDisplacement
		void estimateWaveTrough(); // sparse re-scan of the readback for the deepest current trough (underwater fog boundary)

		// --- Clipmap geometry config (a change rebuilds the mesh) ---
		bool  m_enabled = true;
		float m_seaLevel = 0.0f;   // mirrors the terrain's datum; set every update(), never tweaked here
		float m_ringCell = 0.125f;     // ring 0 cell size (m); doubles per ring. Reach = ringCell*res/2*2^(rings-1)
		int   m_ringRes = 512;         // cells per axis per ring (ring 0 is a full grid, outer rings are annuli)
		int   m_rings = 8;             // ring count (defaults: 16m fine region, ~1km reach)
		// One coarse quad band appended past the outermost ring, stretching its edge lattice out to the
		// camera far plane — the sea meets the horizon in every direction instead of ending at the ring
		// reach. Its inner edge sits on the last ring's fully-morphed (2x cell) lattice at the matching
		// mip, so the seam is watertight by the same CDLOD construction the rings use.
		bool  m_horizonBand = true;
		float m_lastFar = 0.0f;        // camera far plane the current grid was built for (change = rebuild)
		// Bias on the ring-matched displacement mip (negative = sample finer than the ring's Nyquist:
		// slightly more detail, some sampling shimmer while moving). With fixed-cell rings 0 should be fine.
		float m_detailBias = 0.0f;

		// --- Spectrum (TMA/JONSWAP + finite-depth dispersion) + shading; all live via setOceanParams ---
		float m_windSpeed = 20.0f;     // U10 (m/s): the main sea-state knob
		float m_fetchKm = 300.0f;      // wind fetch (km)
		float m_depth = 100.0f;        // ocean depth (m): finite-depth dispersion + TMA attenuation
		float m_windAngle = 5.12f;     // radians, XZ. The swell TRAVELS opposite this — see swellTravelAngle
		// Flow -> wind steering (steeredWindAngle): near a coast the SIM wind turns toward the baked flow
		// so the waves roll toward the local shore; away from any it returns to m_windAngle.
		bool  m_windSteerEnabled = true;
		float m_windSteerRate = 10.0f;    // deg/s the simulation wind may turn (spectrum morphs through it)
		float m_windSteerRange = 400.0f;  // m around the camera whose baked shore directions vote
		float m_steeredWindAngle = 0.0f;  // follows m_windAngle/the flow at the slew rate
		bool  m_windSteerSynced = false;  // adopt m_windAngle on first use instead of turning in from 0
		float m_amplitude = 1.0f;      // artistic scale on the spectrum (1 = physical)
		float m_choppiness = 1.1f;     // horizontal displacement lambda
		float m_normalStrength = 1.0f;
		glm::vec3 m_cascadeSizes = glm::vec3(384.0f, 47.0f, 6.3f); // FFT patch sizes (m)

		glm::vec3 m_absorption = glm::vec3(0.897f, 0.082f, 0.15f);  // Beer-Lambert extinction (1/m)
		glm::vec3 m_scatterColor = glm::vec3(0.047f, 0.1f, 0.15f);
		float m_scatterStrength = 1.0f;
		float m_roughness = 0.07f;
		float m_glintSharpness = 0.75f; // negative mip bias on the FS surface samples: crisper glitter
		float m_glintFilter = 0.5f;     // scale on the roughness-widening variance (spec AA + LEAN)
		float m_sssStrength = 0.66f;     // crest SSS: back-lit crests glow the scatter color, per meter of height
		float m_sssPower = 1.0f;        // crest SSS toward-the-sun view lobe exponent
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

		// --- Shore interaction (driven by the streamer's baked terrain-data map) ---
		float m_shoalScale = 0.02f;     // waves fade below depth = scale * cascade patch size
		float m_shoreFoamDepth = 8.0f;  // surf band: water-column height (m) that churns white; 0 = off
		float m_shoreFoamMax = 0.75f;   // surf band opacity cap: keeps the refracted bottom visible through the foam
		float m_swashAmp = 0.5f;        // swash run-up: un-shoaled wave height riding up the beach (0 = hard cutoff)
		float m_swashDrawdown = 0.0f;   // receding burial depth (m below seabed): deeper = cleaner retreat edge
		float m_troughMargin = 0.35f;   // m the trough is held above the seabed (covers baked-map vs mesh error)
		float m_shoreFoamBias = -0.80f;   // surf fold-threshold shift: negative = sparser/more transparent surf
		float m_swashFlow = 0.5f;       // backflow: horizontal chop on the tongue (recede flows seaward; 0 = off)
		float m_cullMargin = 1.0f;      // VS land cull: footprint buried deeper than this = triangle discarded (0 = off)
		float m_farCullError = 4.0f;    // land cull from the FAR terrain cascade: flat error allowance (m); 0 = near-only

		// --- Ray tracing budget (ocean.fs.glsl per-pixel refraction/reflection rays; all live) ---
		float m_rtRefractionRange = 10.0f;   // max refracted-ray length (m): underwater visibility of traced geometry
		float m_rtReflectionRange = 3000.0f;  // max mirror-ray length (m): how distant scenery still reflects
		float m_rtReflectionMaxRough = 0.25f; // roughness above which the mirror ray is skipped (blurred-sky fallback)
		float m_rtRayCutoffDist = 0.0f;       // camera distance (m) beyond which NO rays trace (analytic
		                                      // bottom + sky fallbacks — the same paths misses take); 0 = unlimited

		// The streamer's active baked terrain-data map (adopted each update()); the shared_ptr keeps this
		// snapshot alive across the frame even while the streamer ships a replacement bake — buoyancy
		// queries (physics, before the next update) and wind steering read it.
		std::shared_ptr<const BakedTerrainData> m_terrainData;

		// CPU copy of the GPU displacement readback tile, refreshed every update() inside the frame's
		// fence-safe window — sampleWaterHeight can then run at ANY point in the frame (physics updates
		// before beginFrame) without racing the GPU rewriting the slot's readback buffer.
		std::vector<uint16> m_dispTile;                  // RGBA16F texels, res^2 per cascade
		uint32 m_dispTileRes = 0;
		float m_waveTrough = 0.0f;      // deepest current trough below the calm level (m; see estimateWaveTrough)
		int   m_waveTroughCooldown = 0; // sparse re-scan counter (the patch minimum is near-stationary)

		bool m_gridDirty = true;

		// container declared first -> destroyed AFTER the node it owns the meshes for
		std::unique_ptr<ObjectContainer> m_container;
		RenderNode m_node;
	};
}
