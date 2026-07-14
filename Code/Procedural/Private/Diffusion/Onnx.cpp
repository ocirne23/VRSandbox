module;

// dml_provider_factory.h drags in d3d12.h/DirectML.h and therefore Windows.h, so the usual guards apply:
// NOMINMAX in particular, or the min/max macros collide with the standard library.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <onnxruntime/onnxruntime_cxx_api.h>
#include <onnxruntime/dml_provider_factory.h>

module Procedural;

import Core;
import Core.Log;
import :Diffusion.Onnx;

namespace Procedural::Diffusion
{
	namespace
	{
		// The ORT environment is a process-wide singleton and is never destroyed (matching the reference,
		// where OrtEnvironment is likewise never closed). Constructed on first use.
		Ort::Env& ortEnv()
		{
			static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "VRSandbox.Diffusion");
			return env;
		}

		std::mutex g_providerMutex;
		std::string g_resolvedProvider;

		void setResolvedProviderOnce(std::string_view p)
		{
			std::lock_guard<std::mutex> lk(g_providerMutex);
			if (!g_resolvedProvider.empty())
				return;
			g_resolvedProvider.assign(p);
			Log::info(std::format("[Diffusion] inference provider: {}", g_resolvedProvider));
		}

		std::wstring widenPath(const std::filesystem::path& p)
		{
			return p.wstring();
		}
	}

	struct OnnxModel::Impl
	{
		std::optional<Ort::Session> session;
		std::string name;
		std::vector<std::string> outputNames; // owned: GetOutputNameAllocated's buffer is transient

		std::atomic<uint64> calls{ 0 };
		std::atomic<uint64> batchItems{ 0 };
		std::atomic<uint64> totalNs{ 0 };
	};

	OnnxModel::RunStats OnnxModel::stats() const
	{
		RunStats s;
		s.calls = m_impl->calls.load(std::memory_order_relaxed);
		s.batchItems = m_impl->batchItems.load(std::memory_order_relaxed);
		s.totalMs = (double)m_impl->totalNs.load(std::memory_order_relaxed) / 1e6;
		return s;
	}

	void OnnxModel::resetStats()
	{
		m_impl->calls.store(0, std::memory_order_relaxed);
		m_impl->batchItems.store(0, std::memory_order_relaxed);
		m_impl->totalNs.store(0, std::memory_order_relaxed);
	}

	OnnxModel::OnnxModel() : m_impl(std::make_unique<Impl>()) {}
	OnnxModel::~OnnxModel() = default;

	bool OnnxModel::isValid() const
	{
		return m_impl && m_impl->session.has_value();
	}

	std::string_view OnnxModel::resolvedProvider()
	{
		std::lock_guard<std::mutex> lk(g_providerMutex);
		return g_resolvedProvider;
	}

	bool OnnxModel::load(const std::filesystem::path& modelPath, std::string_view name, EInferenceDevice device)
	{
		m_impl->name.assign(name);

		std::error_code ec;
		if (!std::filesystem::exists(modelPath, ec))
		{
			Log::error(std::format("[Diffusion] model '{}' not found at {}", name, modelPath.string()));
			return false;
		}

		try
		{
			Ort::SessionOptions opts;
			opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

			bool dml = false;
			if (device != EInferenceDevice::Cpu)
			{
				// DirectML REQUIRES both of these. The Java addDirectML sets them for you; the C++ API does
				// not, and session creation fails without them.
				opts.DisableMemPattern();
				opts.SetExecutionMode(ORT_SEQUENTIAL);
				try
				{
					Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(opts, 0));
					dml = true;
				}
				catch (const Ort::Exception& e)
				{
					if (device == EInferenceDevice::Gpu)
					{
						Log::error(std::format("[Diffusion] DirectML requested but unavailable: {}", e.what()));
						return false;
					}
					Log::warning(std::format("[Diffusion] DirectML unavailable ({}), falling back to CPU. "
					                         "Inference will be very slow.", e.what()));
				}
			}
			setResolvedProviderOnce(dml ? "DirectML" : "CPU");

			const auto t0 = Clock::now();
			m_impl->session.emplace(ortEnv(), widenPath(modelPath).c_str(), opts);
			const auto t1 = Clock::now();

			Ort::AllocatorWithDefaultOptions alloc;
			const size_t nOut = m_impl->session->GetOutputCount();
			m_impl->outputNames.clear();
			for (size_t i = 0; i < nOut; i++)
				m_impl->outputNames.push_back(m_impl->session->GetOutputNameAllocated(i, alloc).get());

			Log::info(std::format("[Diffusion] loaded '{}' in {} ms",
			                      name,
			                      std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()));
			return true;
		}
		catch (const Ort::Exception& e)
		{
			Log::error(std::format("[Diffusion] failed to load model '{}': {}", name, e.what()));
			m_impl->session.reset();
			return false;
		}
	}

	bool OnnxModel::run(std::span<const OnnxInput> inputs, std::vector<float>& out)
	{
		if (!isValid())
			return false;

		const auto t0 = Clock::now();
		try
		{
			const Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);

			std::vector<const char*> names;
			std::vector<Ort::Value> values;
			names.reserve(inputs.size());
			values.reserve(inputs.size());
			for (const OnnxInput& in : inputs)
			{
				// ORT's CreateTensor wants a mutable pointer but does not write through it.
				values.push_back(Ort::Value::CreateTensor<float>(
					mem, const_cast<float*>(in.data.data()), in.data.size(),
					in.shape.data(), in.shape.size()));
				names.push_back(in.name);
			}

			const char* outName = m_impl->outputNames[0].c_str();
			std::vector<Ort::Value> results = m_impl->session->Run(
				Ort::RunOptions{ nullptr }, names.data(), values.data(), values.size(), &outName, 1);

			const Ort::Value& r = results[0];
			const size_t count = r.GetTensorTypeAndShapeInfo().GetElementCount();
			const float* src = r.GetTensorData<float>();
			// Copy out before `results` dies: the buffer belongs to the Ort::Value.
			out.assign(src, src + count);

			m_impl->calls.fetch_add(1, std::memory_order_relaxed);
			// Leading dim of "x" == the batch, so calls vs batchItems shows how well a stage batches.
			if (!inputs.empty() && !inputs[0].shape.empty())
				m_impl->batchItems.fetch_add((uint64)inputs[0].shape[0], std::memory_order_relaxed);
			m_impl->totalNs.fetch_add(
				(uint64)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count(),
				std::memory_order_relaxed);
			return true;
		}
		catch (const Ort::Exception& e)
		{
			Log::error(std::format("[Diffusion] inference failed on '{}': {}", m_impl->name, e.what()));
			return false;
		}
	}
}
