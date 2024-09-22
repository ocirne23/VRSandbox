export module RendererVK.IndirectCullComputePipeline;

import Core;

import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.ComputePipeline;
import RendererVK.Layout;

export class ObjectContainer;

export class IndirectCullComputePipeline final
{
public:
    IndirectCullComputePipeline();
    ~IndirectCullComputePipeline();
    IndirectCullComputePipeline(const IndirectCullComputePipeline&) = delete;

    bool initialize();
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, Buffer& inUbo);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);

    vk::Buffer getIndirectCommandBuffer(uint32 idx) { return m_perFrameData[idx].indirectCommandBuffer.getBuffer(); }
    vk::Buffer getInstanceIdxBuffer(uint32 idx)     { return m_perFrameData[idx].instanceIdxBuffer.getBuffer(); }
    vk::Buffer getInstanceDataBuffer(uint32 idx)    { return m_perFrameData[idx].instanceDataBuffer.getBuffer(); }
    uint32 getMeshInfoCounter(uint32 idx)           { return m_perFrameData[idx].meshInfoCounter; }
    uint32 getInstanceCounter(uint32 idx)           { return m_perFrameData[idx].instanceCounter; }

private:

    ComputePipeline m_computePipeline;

    struct PerFrameData
    {
        uint32 instanceCounter = 0;
        uint32 meshInfoCounter = 0;

        Buffer indirectCommandBuffer;
        Buffer instanceDataBuffer;
        Buffer instanceIdxBuffer;
        Buffer indirectDispatchBuffer;
        Buffer computeMeshInfoBuffer;

        vk::DispatchIndirectCommand* mappedDispatchBuffer = nullptr;
        RendererVKLayout::MeshInfo* mappedMeshInfo = nullptr;
        RendererVKLayout::MeshInstance* mappedMeshInstances = nullptr;
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};