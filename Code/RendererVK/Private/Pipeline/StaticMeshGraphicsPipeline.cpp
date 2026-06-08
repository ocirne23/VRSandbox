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

void StaticMeshGraphicsPipeline::buildPipelineLayout(GraphicsPipelineLayout& graphicsPipelineLayout)
{
    graphicsPipelineLayout.vertexShader.debugFilePath = "Shaders/instanced_indirect.vs.glsl";
    graphicsPipelineLayout.fragmentShader.debugFilePath = "Shaders/instanced_indirect.fs.glsl";

    graphicsPipelineLayout.vertexShader.text = FileSystem::readFileStr(graphicsPipelineLayout.vertexShader.debugFilePath);
    graphicsPipelineLayout.fragmentShader.text = FileSystem::readFileStr(graphicsPipelineLayout.fragmentShader.debugFilePath);

    // Variant 1 (MeshShaderVariant::LitTransparent): same lit shader, alpha-blended, no depth write.
    graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
        .fragmentShader = ShaderSource{
            .text = graphicsPipelineLayout.fragmentShader.text,
            .debugFilePath = graphicsPipelineLayout.fragmentShader.debugFilePath,
        },
        .blendEnable = true,
        .depthWrite = false,
    });
    // Variant 2 (MeshShaderVariant::UnlitOpaque): unlit opaque.
    const std::string unlitVariantPath = "Shaders/instanced_indirect_unlit.fs.glsl";
    graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
        .fragmentShader = ShaderSource{
            .text = FileSystem::readFileStr(unlitVariantPath),
            .debugFilePath = unlitVariantPath,
        },
    });
	// Variant 3 (MeshShaderVariant::UnlitTransparent): same unlit shader, alpha-blended, no depth write.
	graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
		.fragmentShader = ShaderSource{
			.text = graphicsPipelineLayout.additionalVariants[0].fragmentShader.text,
			.debugFilePath = graphicsPipelineLayout.additionalVariants[0].fragmentShader.debugFilePath,
		},
		.blendEnable = true,
		.depthWrite = false,
	});

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
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_shadowMap (sun CSM, comparison)
        .binding = 8,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_shadowMapDepth (raw depth, PCSS blocker search)
        .binding = 9,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // probe_sh (GI volume)
        .binding = 10,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // gi volume min (ivec4)
        .binding = 11,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_ao (denoised screen-space AO)
        .binding = 13,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });

    // Only the AO binding is refreshed after the (cached) draw CB is recorded, so flag it
    // UPDATE_AFTER_BIND. Parallel to descriptorSetBindings; all other bindings keep default (no) flags.
    graphicsPipelineLayout.descriptorBindingFlags.resize(descriptorSetBindings.size());
    graphicsPipelineLayout.descriptorBindingFlags.back() = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
}

void StaticMeshGraphicsPipeline::updateAODescriptor(vk::DescriptorSet descriptorSet, vk::ImageView aoView, vk::Sampler aoSampler)
{
    // The cached draw command buffer binds this set by handle, so refreshing the AO image binding each frame
    // keeps the forward pass pointed at this frame's denoised AO image (recreated on resize, ping-ponged
    // per frame). The AO images stay in GENERAL layout (written by the compute denoise, sampled here).
    vk::DescriptorImageInfo imageInfo{ .sampler = aoSampler, .imageView = aoView, .imageLayout = vk::ImageLayout::eGeneral };
    vk::WriteDescriptorSet write{ .dstSet = descriptorSet, .dstBinding = 13, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &imageInfo };
    Globals::device.getDevice().updateDescriptorSets(1, &write, 0, nullptr);
}

void StaticMeshGraphicsPipeline::initialize(RenderPass& renderPass)
{
    m_pRenderPass = &renderPass;
    m_sampler.initialize();

    GraphicsPipelineLayout graphicsPipelineLayout;
    buildPipelineLayout(graphicsPipelineLayout);
    m_graphicsPipeline.initialize(renderPass, graphicsPipelineLayout);

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

void StaticMeshGraphicsPipeline::reloadShaders()
{
    if (!m_pRenderPass)
        return;

    GraphicsPipelineLayout graphicsPipelineLayout;
    buildPipelineLayout(graphicsPipelineLayout);
    if (!m_graphicsPipeline.reloadShaders(*m_pRenderPass, graphicsPipelineLayout))
    {
        printf("StaticMeshGraphicsPipeline: shader reload failed, keeping previous pipeline\n");
        return;
    }

    m_indirectExecutionSet.destroy();
    m_indirectExecutionSet.initialize(m_graphicsPipeline);
}

void StaticMeshGraphicsPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params)
{
    std::array<DescriptorSetUpdateInfo, 11> graphicsDescriptorSetUpdateInfos
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
            .binding = 8,
            .type = vk::DescriptorType::eCombinedImageSampler,
            .imageInfos = {
                vk::DescriptorImageInfo {
                    .sampler = params.shadowMapSampler,
                    .imageView = params.shadowMapView,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 9,
            .type = vk::DescriptorType::eCombinedImageSampler,
            .imageInfos = {
                vk::DescriptorImageInfo {
                    .sampler = params.shadowMapDepthSampler,
                    .imageView = params.shadowMapView,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            }
        },
        DescriptorSetUpdateInfo{
            .binding = 10,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo { .buffer = params.giGridDataBuffer.getBuffer(), .range = params.giGridDataBuffer.getSize() } }
        },
        DescriptorSetUpdateInfo{
            .binding = 11,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo { .buffer = params.giTableBuffer.getBuffer(), .range = params.giTableBuffer.getSize() } }
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
    recordExecuteGeneratedCommands(vkCommandBuffer, params.indirectCommandBuffer, m_preprocessBuffers[frameIdx], numMeshes);
    recordExecuteGeneratedCommands(vkCommandBuffer, params.indirectCommandBuffer, m_transparentPreprocessBuffers[frameIdx], numMeshes,
        RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::IndirectDrawSequence));
}

void StaticMeshGraphicsPipeline::recordExecuteGeneratedCommands(vk::CommandBuffer vkCommandBuffer, Buffer& indirectCommandBuffer, Buffer& preprocessBuffer, uint32 numMeshes, vk::DeviceSize indirectOffset)
{
    vk::GeneratedCommandsInfoEXT generatedCommandsInfo{
        .shaderStages = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .indirectExecutionSet = m_indirectExecutionSet.getHandle(),
        .indirectCommandsLayout = m_indirectCommandsLayout.getHandle(),
        .indirectAddress = indirectCommandBuffer.getDeviceAddress() + indirectOffset,
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
