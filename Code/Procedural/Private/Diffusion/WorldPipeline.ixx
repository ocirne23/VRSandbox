export module Procedural:Diffusion.Pipeline;

import Core;
import :Diffusion.ModelAssets;
import :Diffusion.InfiniteTensor;
import :Diffusion.Onnx;
import :Diffusion.SyntheticMap;
import :Diffusion.Tensor;

// The Terrain Diffusion world pipeline (port of the reference's WorldPipeline / world_pipeline.py).
//
// Three stages, each an InfiniteTensor over its own pixel space:
//   coarse   64x64 tiles, stride 48, 20-step DPM-Solver++     -> 7ch (6 data + weight)
//   latent   64x64 tiles, stride 32, 2 flow-matching steps    -> 6ch (5 data + weight)   [M2]
//   decoder  256x256 tiles, stride 192, 1 flow-matching step  -> 2ch (1 data + weight)   [M2]
//
// Coordinate spine (latentCompression = lc = 8 in the shipped config):
//   1 latent tile INDEX step == 1 coarse pixel  =>  1 coarse pixel = 32*lc = 256 native px = 7.68 km at 30 m/px
//
// NOT thread-safe (MemoryTileStore isn't). Callers must serialise; TerrainGenV3 uses a single inference
// thread for exactly this reason.
export namespace Procedural::Diffusion
{
	class WorldPipeline
	{
	public:
		WorldPipeline();
		~WorldPipeline();
		WorldPipeline(const WorldPipeline&) = delete;
		WorldPipeline& operator=(const WorldPipeline&) = delete;

		// Loads the models and builds the stage graph. `coarseOnly` skips base/decoder (M1 bring-up: the
		// coarse model is 22 MB against 2.25 GB for the other two).
		// Returns false on any failure, after logging; the pipeline is then unusable.
		bool initialize(uint64 seed, const ModelConfig& cfg, const PipelineData& data,
		                EInferenceDevice device, bool coarseOnly);
		bool isValid() const;

		// Cheap reseed: swaps the synthetic map and drops the tile caches. The models stay loaded — this is
		// the ONLY sane way to change seed, since reloading is 2.28 GB of ONNX.
		void setSeed(uint64 newSeed);
		uint64 seed() const { return m_seed; }

		const ModelConfig& config() const { return m_config; }

		// Slice of the coarse stage, shape [7, ci1-ci0, cj1-cj0], in COARSE pixel units (1 = 256 native px).
		// Channels 0..5 are weighted sums; channel 6 is the blend weight — divide to recover values.
		// Channel meanings (after the /weight divide): 0 elev_sqrt, 1 p5, 2 temp, 3 temp_std,
		// 4 precip, 5 precip_std.
		FloatTensor getCoarseSlice(int32 ci0, int32 cj0, int32 ci1, int32 cj1);

		// Native pixels per coarse pixel (32 * latentCompression = 256 in the shipped config). One coarse
		// pixel is 7.68 km at 30 m/px, which is why the coarse stage alone can answer wide-area queries
		// that the full pipeline never could.
		int32 nativePerCoarsePixel() const { return 32 * m_config.latentCompression; }

		// The full-resolution result for the half-open NATIVE-pixel rect [i1,i2) x [j1,j2) (i = row/z,
		// j = column/x). Requires the full pipeline (coarseOnly == false).
		//   outElev    -> H*W metres above sea level (negative = seabed)
		//   outMacro   -> H*W, optional: the LOW-FREQUENCY band of the same elevation, i.e. the smooth
		//                 surface the high-frequency residual rides on. Free — the pipeline builds elevation
		//                 as residual + lowres and this is that lowres term, decompressed the same way. The
		//                 difference (elev - macro) is the local relief that tells a crag from flat ground at
		//                 altitude, which is what the terrain shader's rock layer keys on.
		//   outClimate -> 5*H*W, channel-major, only when withClimate:
		//                 0 temperature C (lapse-corrected to the native elevation)
		//                 1 temperature seasonality
		//                 2 precipitation mm/yr
		//                 3 precipitation seasonality
		//                 4 lapse rate beta (C/m)
		// Returns false if the pipeline is coarse-only or inference failed.
		bool get(int32 i1, int32 j1, int32 i2, int32 j2, bool withClimate,
		         std::vector<float>& outElev, std::vector<float>& outClimate,
		         std::vector<float>* outMacro = nullptr);

		uint64 totalComputedWindowCount() const;
		size_t residentCacheBytes() const;
		// "coarse 180 calls/180 items 372ms | base ..." — which stage actually dominates.
		std::string inferenceStatsText() const;
		void resetInferenceStats();

	private:
		void buildCoarseStage();
		void buildLatentStage();
		void buildDecoderStage();

		// Runs the 20-step sampler for a whole batch of coarse tiles in lockstep (they share one sigma
		// schedule, so step k of every tile can go in one dispatch). The reference does one tile at a time;
		// the coarse model is dispatch-bound at 64x64, so batching is close to free throughput.
		std::vector<FloatTensor> coarseBatch(std::span<const WindowKey> windowIndices);
		// One batched flow-matching step of the latent stage. prevSamples is null for the first (init) step.
		std::vector<FloatTensor> latentBatch(std::span<const WindowKey> windowIndices,
		                                     const std::vector<FloatTensor>* prevSamples,
		                                     const std::vector<FloatTensor>& coarseSlices,
		                                     float t, uint64 seedOffset);
		// The 58-dim mp_concat conditioning vector built from a (7,4,4) coarse patch.
		void buildLatentConditioning(const FloatTensor& coarseSlice, float* out58) const;
		FloatTensor decoderTile(std::span<const int32> windowIndex, const FloatTensor& latentSlice, float t);

		void computeElev(int32 i1, int32 j1, int32 i2, int32 j2, std::vector<float>& outElev,
		                 std::vector<float>* outMacro);
		void computeClimate(int32 i1, int32 j1, int32 i2, int32 j2, std::span<const float> elev,
		                    int32 H, int32 W, std::vector<float>& outClimate);

		uint64 m_seed = 0;
		ModelConfig m_config;
		PipelineData m_data;
		bool m_coarseOnly = true;

		OnnxModel m_coarseModel;
		OnnxModel m_baseModel;
		OnnxModel m_decoderModel;
		std::unique_ptr<SyntheticMapFactory> m_syntheticMap;
		std::unique_ptr<MemoryTileStore> m_tileStore;

		// All owned by m_tileStore. The latent stage is a 2-step chain: init -> step0.
		InfiniteTensor* m_coarse = nullptr;
		InfiniteTensor* m_initLatent = nullptr;
		InfiniteTensor* m_latents = nullptr;
		InfiniteTensor* m_residual = nullptr;

		std::vector<float> m_ww64;     // tent window for the 64x64 coarse/latent tiles
		std::vector<float> m_ww256;    // tent window for the 256x256 decoder tiles
		std::vector<float> m_condVals; // log(condSnr[i] / 8)
		std::vector<float> m_mpConcatScales;
		bool m_inferenceFailed = false; // sticky: a failed tile must not be silently cached as terrain
		bool m_valid = false;
	};
}
