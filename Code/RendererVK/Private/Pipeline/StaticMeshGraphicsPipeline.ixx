export module RendererVK.StaticMeshGraphicsPipeline;

import Core;

import RendererVK.GraphicsPipeline;
//import RendererVK.Texture;
//import RendererVK.Sampler;

export class ObjectContainer;
export class IndirectCullComputePipeline;
export class MeshDataManager;
export class StagingManager;
export class Buffer;
export class CommandBuffer;

export class StaticMeshGraphicsPipeline final
{
public:
    StaticMeshGraphicsPipeline();
    ~StaticMeshGraphicsPipeline();
    StaticMeshGraphicsPipeline(const StaticMeshGraphicsPipeline&) = delete;

    bool initialize(RenderPass& renderPass, StagingManager& stagingManager);
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, Buffer& inUbo, Buffer& materialInfo, IndirectCullComputePipeline& indirectCullPipeline, MeshDataManager& meshDataManager);
    void update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers);

private:

    GraphicsPipeline m_graphicsPipeline;

    //Texture m_colorTex;
    //Texture m_normalTex;
    //Texture m_rmhTex;
    //Sampler m_sampler;
};