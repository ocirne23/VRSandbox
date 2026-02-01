export module RendererVK:LightGridComputePipeline;

import Core;

import :VK;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :DescriptorSet;
import :Layout;

export class LightGridComputePipeline final
{
public:

	void initialize();
	struct RecordParams
	{
		DescriptorSet& descriptorSet;
		Buffer& inLightInfoBuffer;
		Buffer& outLightGridBuffer;
		Buffer& outLightTableBuffer;
	};
	void record(CommandBuffer& commandBuffer, uint32 frameIdx, RecordParams& recordParams);
	void update(uint32 frameIdx, uint32 numLights);
	vk::DescriptorSetLayout getDescriptorSetLayout() const { return m_computePipeline.getDescriptorSetLayout(); }

private:
	struct PerFrameData
	{
		Buffer inIndirectCommandBuffer;
		std::span<vk::DispatchIndirectCommand> mappedIndirectCommands;
	};
	std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
	ComputePipeline m_computePipeline;
};