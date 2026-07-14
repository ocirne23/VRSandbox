export module Procedural:Diffusion.Scheduler;

import Core;

// EDM / DPM-Solver++ sampler for the coarse stage.
//
// Port of the Java mod's EDMScheduler, itself a port of HuggingFace's EDMDPMSolverMultistepScheduler
// (dpmsolver.py), restricted to the single configuration the pipeline uses:
//     algorithm_type="dpmsolver++", solver_type="midpoint", sigma_schedule="karras",
//     final_sigmas_type="zero", solver_order=2
//
// Upstream lineage is Apache-2.0 (TSAIL Team / The HuggingFace Team, strongly influenced by
// https://github.com/LuChengTHU/dpm-solver and https://github.com/NVlabs/edm). See Dependencies/Licenses.txt.
//
// Instances are per-tile and hold mutable step state — not reentrant, not shareable.
export namespace Procedural::Diffusion
{
	class EDMScheduler
	{
	public:
		static constexpr float SIGMA_DATA = 0.5f;
		static constexpr float SIGMA_MIN = 0.002f;
		static constexpr float SIGMA_MAX = 80.0f;

		explicit EDMScheduler(int32 numSteps);
		void reset();

		// One DPM-Solver++ step, updating `sample` in place to the next (lower) sigma.
		// Step 0 is first-order (no history), 1..n-2 second-order, and the last step is FORCED back to
		// first-order: sigma_t is 0 there, so the second-order form would evaluate -log(0).
		// Takes a span so a batched caller can step one tile's slice of a shared buffer in place.
		void step(std::span<const float> modelOut, std::span<float> sample);

		// c_in scaling: out = sample / sqrt(sigma^2 + sigma_data^2).
		static void preconditionInputs(std::span<const float> sample, float sigma, std::span<float> out);

		// The value fed to the model's `noise_labels` input: atan(sigma / sigma_data).
		// NOTE this is what the pipeline actually uses — NOT the `timesteps` array, which the reference
		// computes and never reads. Deliberately not ported.
		static float trigflowPreconditionNoise(float sigma);

		// sigmas[i] = (max^(1/rho) + i/(n-1) * (min^(1/rho) - max^(1/rho)))^rho, then a hard 0.
		// Length n+1, descending 80 -> 0.002 -> 0.
		static std::vector<float> computeKarrasSigmas(int32 n);

		const std::vector<float>& sigmas() const { return m_sigmas; }

	private:
		static void preconditionOutputs(std::span<const float> sample, std::span<const float> modelOut,
		                                float sigma, std::span<float> x0Out);
		static void firstOrderUpdate(std::span<const float> x0Pred, std::span<float> sample,
		                             float sigmaS, float sigmaT);
		static void secondOrderUpdate(std::span<const float> m1, std::span<const float> m0,
		                              std::span<float> sample, float sigmaS0, float sigmaT, float sigmaS1);

		std::vector<float> m_sigmas;
		std::vector<float> m_prevModelOutput; // x0 prediction from the previous step
		std::vector<float> m_x0Pred;          // scratch, swapped into m_prevModelOutput each step
		int32 m_numSteps = 0;
		int32 m_stepIndex = 0;
		int32 m_lowerOrderNums = 0;
	};
}
