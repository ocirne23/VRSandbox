module RendererVK:StaticMeshGraphicsPipeline;

import Core;

import File.FileSystem;

import :Buffer;
import :CommandBuffer;
import :Device;
import :Layout;
import :StagingManager;
import :IndirectCullComputePipeline;
import :TextureManager;

StaticMeshGraphicsPipeline::StaticMeshGraphicsPipeline() {}
StaticMeshGraphicsPipeline::~StaticMeshGraphicsPipeline() {}

void StaticMeshGraphicsPipeline::initialize(RenderPass& renderPass)
{
    m_sampler.initialize();
    GraphicsPipelineLayout graphicsPipelineLayout;
    graphicsPipelineLayout.vertexShaderDebugFilePath = "Shaders/instanced_indirect.vs.glsl";
    graphicsPipelineLayout.fragmentShaderDebugFilePath = "Shaders/instanced_indirect.fs.glsl";

    graphicsPipelineLayout.vertexShaderText = FileSystem::readFileStr(graphicsPipelineLayout.vertexShaderDebugFilePath);
    graphicsPipelineLayout.fragmentShaderText = FileSystem::readFileStr(graphicsPipelineLayout.fragmentShaderDebugFilePath);

    // When device-generated commands are available, build additional fragment-shader variants that
    // share this pipeline layout. These become the per-draw selectable pipelines of the Indirect
    // Execution Set; the compute cull pass will pick a variant per draw in a later phase.
    const bool dgcSupported = Globals::device.supportsDeviceGeneratedCommands();
    if (dgcSupported)
    {
        graphicsPipelineLayout.indirectBindable = true;
        // Variant 1 (MeshShaderVariant::Unlit): unlit opaque.
        const std::string unlitVariantPath = "Shaders/instanced_indirect_unlit.fs.glsl";
        graphicsPipelineLayout.fragmentShaderVariants.push_back(ShaderVariant{
            .text = FileSystem::readFileStr(unlitVariantPath),
            .debugFilePath = unlitVariantPath,
        });
        // Variant 2 (MeshShaderVariant::LitTransparent): same lit shader, alpha-blended, no depth write.
        graphicsPipelineLayout.fragmentShaderVariants.push_back(ShaderVariant{
            .text = graphicsPipelineLayout.fragmentShaderText,
            .debugFilePath = graphicsPipelineLayout.fragmentShaderDebugFilePath,
            .blendEnable = true,
            .depthWrite = false,
        });
    }

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
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InLightInfos
        .binding = 4,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InLightGrid
        .binding = 5,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InGridTable
        .binding = 6,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_textures
        .binding = 7,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = RendererVKLayout::MAX_TEXTURES,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    m_graphicsPipeline.initialize(renderPass, graphicsPipelineLayout);

    m_useDeviceGeneratedCommands = dgcSupported;
    if (m_useDeviceGeneratedCommands)
    {
        m_indirectExecutionSet.initialize(m_graphicsPipeline);
        m_indirectCommandsLayout.initialize(m_graphicsPipeline.getPipelineLayout(),
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

        // Size the preprocess scratch buffer for the worst case (one sequence per unique mesh).
        vk::GeneratedCommandsMemoryRequirementsInfoEXT memReqInfo{
            .indirectExecutionSet = m_indirectExecutionSet.getHandle(),
            .indirectCommandsLayout = m_indirectCommandsLayout.getHandle(),
            .maxSequenceCount = RendererVKLayout::MAX_UNIQUE_MESHES,
            .maxDrawCount = RendererVKLayout::MAX_UNIQUE_MESHES,
        };
        vk::MemoryRequirements2 memReq;
        Globals::device.getDevice().getGeneratedCommandsMemoryRequirementsEXT(&memReqInfo, &memReq);
        m_preprocessSize = memReq.memoryRequirements.size;
        if (m_preprocessSize > 0)
        {
            // Separate scratch per pass so the opaque and transparent executes don't alias preprocess memory.
            for (uint32 i = 0; i < RendererVKLayout::NUM_FRAMES_IN_FLIGHT; i++)
            {
                m_preprocessBuffers[i].initialize(m_preprocessSize,
                    {}, // all usage bits supplied via usage2 below
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::BufferUsageFlagBits2::ePreprocessBufferEXT | vk::BufferUsageFlagBits2::eShaderDeviceAddress);
                m_transparentPreprocessBuffers[i].initialize(m_preprocessSize,
                    {},
                    vk::MemoryPropertyFlagBits::eDeviceLocal,
                    vk::BufferUsageFlagBits2::ePreprocessBufferEXT | vk::BufferUsageFlagBits2::eShaderDeviceAddress);
            }
        }
    }
}

void StaticMeshGraphicsPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params)
{
    std::array<DescriptorSetUpdateInfo, 7> graphicsDescriptorSetUpdateInfos
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
                    .range = params.meshInstanceBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 2,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.materialInfoBuffer.getBuffer(),
                    .range = params.materialInfoBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 4,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.lightInfosBuffer.getBuffer(),
                    .range = params.lightInfosBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 5,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.lightGridsBuffer.getBuffer(),
                    .range = params.lightGridsBuffer.getSize(),
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 6,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = {
                vk::DescriptorBufferInfo {
                    .buffer = params.lightTableBuffer.getBuffer(),
                    .range = params.lightTableBuffer.getSize(),
                }
            }
        },

        DescriptorSetUpdateInfo{
            .binding = 7,
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
    // The cull pass writes two buffers of RendererVKLayout::IndirectDrawSequence (pipelineIndex +
    // draw command), one per unique mesh, bucketed into opaque and transparent by material alpha mode.
    if (m_useDeviceGeneratedCommands)
    {
        // Opaque pass, then transparent pass. Both execute in this subpass in submission order, so
        // transparent draws (blend on, depth write off) blend over the opaque result and test against
        // opaque depth. Each sequence's EXECUTION_SET token selects its pipeline variant; the bound
        // pipeline above is just the set's initial pipeline (state template for preprocessing).
        recordExecuteGeneratedCommands(vkCommandBuffer, params.indirectCommandBuffer, m_preprocessBuffers[frameIdx], numMeshes);
        recordExecuteGeneratedCommands(vkCommandBuffer, params.transparentIndirectCommandBuffer, m_transparentPreprocessBuffers[frameIdx], numMeshes);
    }
    else
    {
        // Fallback (no device-generated commands): opaque draws only, reading the embedded
        // VkDrawIndexedIndirectCommand directly (skip the leading pipelineIndex at offset 4,
        // stride = the sequence size). Transparency requires the DGC path.
        vkCommandBuffer.drawIndexedIndirect(params.indirectCommandBuffer.getBuffer(),
            offsetof(RendererVKLayout::IndirectDrawSequence, indexCount), numMeshes,
            sizeof(RendererVKLayout::IndirectDrawSequence));
    }
}

void StaticMeshGraphicsPipeline::recordExecuteGeneratedCommands(vk::CommandBuffer vkCommandBuffer, Buffer& indirectCommandBuffer, Buffer& preprocessBuffer, uint32 numMeshes)
{
    vk::GeneratedCommandsInfoEXT generatedCommandsInfo{
        .shaderStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .indirectExecutionSet = m_indirectExecutionSet.getHandle(),
        .indirectCommandsLayout = m_indirectCommandsLayout.getHandle(),
        .indirectAddress = indirectCommandBuffer.getDeviceAddress(),
        .indirectAddressSize = numMeshes * sizeof(RendererVKLayout::IndirectDrawSequence),
        .preprocessAddress = m_preprocessSize > 0 ? preprocessBuffer.getDeviceAddress() : 0,
        .preprocessSize = m_preprocessSize,
        .maxSequenceCount = numMeshes,
        .sequenceCountAddress = 0, // exactly maxSequenceCount sequences; empty meshes are no-op draws
        .maxDrawCount = numMeshes,
    };
    vkCommandBuffer.executeGeneratedCommandsEXT(vk::False, generatedCommandsInfo);
}

void StaticMeshGraphicsPipeline::update(uint32 frameIdx, std::vector<ObjectContainer*>& objectContainers)
{
}
