export module RendererVK:ClusteredShadingComputePipeline;

import Core;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;

export class ClusteredShadingComputePipeline final
{
public:

	bool initialize();
	struct RecordParams
	{
		Buffer& ubo;
		Buffer& inLightData;
		Buffer& outLightData;
	};
	void record(CommandBuffer& commandBuffer, uint32 frameIdx, RecordParams& recordParams);
	void update(uint32 frameIdx);

private:

	ComputePipeline m_computePipeline;
};