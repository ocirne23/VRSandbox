module RendererVK:StaticMeshGraphicsPipeline;

import Core;

import File.FileSystem;

import :Buffer;
import :CommandBuffer;
import :Layout;
import :StagingManager;
import :IndirectCullComputePipeline;
import :TextureManager;

StaticMeshGraphicsPipeline::StaticMeshGraphicsPipeline() {}
StaticMeshGraphicsPipeline::~StaticMeshGraphicsPipeline() {}

bool StaticMeshGraphicsPipeline::initialize(RenderPass& renderPass)
{
    m_sampler.initialize();
    GraphicsPipelineLayout graphicsPipelineLayout;
    graphicsPipelineLayout.vertexShaderDebugFilePath = "Shaders/instanced_indirect.vs.glsl";
    graphicsPipelineLayout.fragmentShaderDebugFilePath = "Shaders/instanced_indirect.fs.glsl";

    graphicsPipelineLayout.vertexShaderText = FileSystem::readFileStr(graphicsPipelineLayout.vertexShaderDebugFilePath);
    graphicsPipelineLayout.fragmentShaderText = FileSystem::readFileStr(graphicsPipelineLayout.fragmentShaderDebugFilePath);

    auto& bindingDescriptions = graphicsPipelineLayout.vertexLayoutInfo.bindingDescriptions;
    bindingDescriptions.push_back(vk::VertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(RendererVKLayout::MeshVertex),
        .inputRate = vk::VertexInputRate::eVertex,
    });
    bindingDescriptions.push_back(vk::VertexInputBindingDescription{
        .binding = 2,
        .stride = sizeof(uint32),
        .inputRate = vk::VertexInputRate::eInstance,
    });

    auto& attributeDescriptions = graphicsPipelineLayout.vertexLayoutInfo.attributeDescriptions;
    attributeDescriptions.push_back(vk::VertexInputAttributeDescription{ // position
        .location = 0,
        .binding = 0,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(RendererVKLayout::MeshVertex, position),
    });
    attributeDescriptions.push_back(vk::VertexInputAttributeDescription{ // normals
        .location = 1,
        .binding = 0,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(RendererVKLayout::MeshVertex, normal),
    });
    attributeDescriptions.push_back(vk::VertexInputAttributeDescription{ // tangents
        .location = 2,
        .binding = 0,
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = offsetof(RendererVKLayout::MeshVertex, tangent),
    });
    attributeDescriptions.push_back(vk::VertexInputAttributeDescription{ // texcoords
        .location = 3,
        .binding = 0,
        .format = vk::Format::eR32G32Sfloat,
        .offset = offsetof(RendererVKLayout::MeshVertex, texCoord),
    });
    attributeDescriptions.push_back(vk::VertexInputAttributeDescription{ // inst_idx
        .location = 4,
        .binding = 2,
        .format = vk::Format::eR32Uint,
        .offset = 0,
    });

    auto& descriptorSetBindings = graphicsPipelineLayout.descriptorSetLayoutBindings;
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // UBO
        .binding = 0,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment
    });

    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InRenderNodeTransforms
        .binding = 3,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInstances
        .binding = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMaterialInfos
        .binding = 2,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });

    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_color
        .binding = 4,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1024,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    m_graphicsPipeline.initialize(renderPass, graphicsPipelineLayout);

    return true;
}

void StaticMeshGraphicsPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params)
{
    std::array<DescriptorSetUpdateInfo, 4> graphicsDescriptorSetUpdateInfos
    {
        DescriptorSetUpdateInfo{
            .binding = 0,
            .type = vk::DescriptorType::eUniformBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.ubo.getBuffer(),
                    .range = sizeof(RendererVKLayout::Ubo),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 1,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.meshInstanceBuffer.getBuffer(),
                    .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::OutMeshInstance),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 2,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.materialInfoBuffer.getBuffer(),
                    .range = RendererVKLayout::MAX_UNIQUE_MATERIALS * sizeof(RendererVKLayout::MaterialInfo),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 4,
            .type = vk::DescriptorType::eCombinedImageSampler,
        },
    };

    const std::vector<Texture>& textures = Globals::textureManager.getTextures();
    for (const Texture& tex : textures)
    {
        graphicsDescriptorSetUpdateInfos.back().imageInfos.push_back(
            vk::DescriptorImageInfo{
                .sampler = m_sampler.getSampler(),
                .imageView = tex.getImageView(),
                .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            }
        );
    }

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    vk::DescriptorSet descriptorSet = params.descriptorSet.getDescriptorSet();
    commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, descriptorSet, graphicsDescriptorSetUpdateInfos);
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
    vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);
    vkCommandBuffer.bindVertexBuffers(0, { params.vertexBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindVertexBuffers(2, { params.instanceIdxBuffer.getBuffer() }, {0});
    vkCommandBuffer.bindIndexBuffer(params.indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
    vkCommandBuffer.drawIndexedIndirect(params.indirectCommandBuffer.getBuffer(), 0, numMeshes, sizeof(vk::DrawIndexedIndirectCommand));
}

void StaticMeshGraphicsPipeline::update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers)
{
}
