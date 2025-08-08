module RendererVK.StaticMeshGraphicsPipeline;

import Core;

import File.FileSystem;

import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.Layout;
import RendererVK.StagingManager;
import RendererVK.MeshDataManager;
import RendererVK.IndirectCullComputePipeline;

StaticMeshGraphicsPipeline::StaticMeshGraphicsPipeline() {}
StaticMeshGraphicsPipeline::~StaticMeshGraphicsPipeline() {}

bool StaticMeshGraphicsPipeline::initialize(RenderPass& renderPass, StagingManager& stagingManager)
{
    //m_sampler.initialize();
    //m_colorTex.initialize(stagingManager, "Textures/boat/color.dds", true);
    //m_normalTex.initialize(stagingManager, "Textures/boat/normal.dds", false);
    //m_rmhTex.initialize(stagingManager, "Textures/boat/roughness_metallic_height.dds", false);

    GraphicsPipelineLayout graphicsPipelineLayout;
    graphicsPipelineLayout.vertexShaderText = std::move(FileSystem::readFileStr("Shaders/instanced_indirect.vs.glsl"));
    graphicsPipelineLayout.fragmentShaderText = std::move(FileSystem::readFileStr("Shaders/instanced_indirect.fs.glsl"));

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

    //descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_color
    //    .binding = 3,
    //    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
    //    .descriptorCount = 1,
    //    .stageFlags = vk::ShaderStageFlagBits::eFragment
    //});
    //descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_normal
    //    .binding = 4,
    //    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
    //    .descriptorCount = 1,
    //    .stageFlags = vk::ShaderStageFlagBits::eFragment
    //});
    //descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_roughness_metallic_height
    //    .binding = 5,
    //    .descriptorType = vk::DescriptorType::eCombinedImageSampler,
    //    .descriptorCount = 1,
    //    .stageFlags = vk::ShaderStageFlagBits::eFragment
    //});
    m_graphicsPipeline.initialize(renderPass, graphicsPipelineLayout);

    return true;
}

void StaticMeshGraphicsPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params)
{
    std::array<DescriptorSetUpdateInfo, 3> graphicsDescriptorSetUpdateInfos
    {
        DescriptorSetUpdateInfo{
            .binding = 0,
            .type = vk::DescriptorType::eUniformBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = params.ubo.getBuffer(),
                .range = sizeof(RendererVKLayout::Ubo),
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 1,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = params.meshInstanceBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::OutMeshInstance),
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 2,
            .type = vk::DescriptorType::eStorageBuffer,
            .info = vk::DescriptorBufferInfo {
                .buffer = params.materialInfoBuffer.getBuffer(),
                .range = RendererVKLayout::MAX_UNIQUE_MATERIALS * sizeof(RendererVKLayout::MaterialInfo),
            }
        }//,
        //DescriptorSetUpdateInfo{
        //    .binding = 3,
        //    .type = vk::DescriptorType::eCombinedImageSampler,
        //    .info = vk::DescriptorImageInfo {
        //        .sampler = m_sampler.getSampler(),
        //        .imageView = m_colorTex.getImageView(),
        //        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        //    }
        //},
        //DescriptorSetUpdateInfo{
        //    .binding = 4,
        //    .type = vk::DescriptorType::eCombinedImageSampler,
        //    .info = vk::DescriptorImageInfo {
        //        .sampler = m_sampler.getSampler(),
        //        .imageView = m_normalTex.getImageView(),
        //        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        //    }
        //},
        //DescriptorSetUpdateInfo{
        //    .binding = 5,
        //    .type = vk::DescriptorType::eCombinedImageSampler,
        //    .info = vk::DescriptorImageInfo {
        //        .sampler = m_sampler.getSampler(),
        //        .imageView = m_rmhTex.getImageView(),
        //        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        //    }
        //}
    };

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, graphicsDescriptorSetUpdateInfos);
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
    vkCommandBuffer.bindVertexBuffers(0, { params.vertexBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindVertexBuffers(2, { params.instanceIdxBuffer.getBuffer() }, {0});
    vkCommandBuffer.bindIndexBuffer(params.indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
    vkCommandBuffer.drawIndexedIndirect(params.indirectCommandBuffer.getBuffer(), 0, numMeshes, sizeof(vk::DrawIndexedIndirectCommand));
}

void StaticMeshGraphicsPipeline::update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers)
{
}
