export module RendererVK.ClusteredShadingComputePipeline;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.ComputePipeline;

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