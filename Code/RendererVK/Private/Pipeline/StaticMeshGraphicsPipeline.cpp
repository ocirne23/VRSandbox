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

void StaticMeshGraphicsPipeline::buildPipelineLayout(GraphicsPipelineLayout& graphicsPipelineLayout, uint32 maxTextures)
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
	const std::string unlitVariantText = FileSystem::readFileStr(unlitVariantPath);
    graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
        .fragmentShader = ShaderSource{
            .text = unlitVariantText,
            .debugFilePath = unlitVariantPath,
        },
    });
	// Variant 3 (MeshShaderVariant::UnlitTransparent): same unlit shader, alpha-blended, no depth write.
	graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
		.fragmentShader = ShaderSource{
			.text = unlitVariantText,
			.debugFilePath = unlitVariantPath,
		},
		.blendEnable = true,
		.depthWrite = false,
	});
	// Variant 4 (EPipelineIndex::Sky): analytic sky + sun disc, for the inside of the sky sphere.
	const std::string skyVariantPath = "Shaders/sky.fs.glsl";
	const std::string skyVariantText = FileSystem::readFileStr(skyVariantPath);
	graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
		.fragmentShader = ShaderSource{
			.text = skyVariantText,
			.debugFilePath = skyVariantPath,
		},
	});
	// Variants 5-7 (Wireframe + gizmos) all shade by vertex position (debug color).
    const std::string& gizmoVariantPath = unlitVariantPath;
	const std::string& gizmoVariantText = unlitVariantText;
	// Variant 5 (EPipelineIndex::WireframeTransparent): tangent-debug color, drawn as lines.
	graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
		.fragmentShader = ShaderSource{
			.text = gizmoVariantText,
			.debugFilePath = gizmoVariantPath,
            .defines = { },
		},
		.blendEnable = false,
		.depthWrite = true,
		.polygonMode = vk::PolygonMode::eLine,
		.cullMode = vk::CullModeFlagBits::eNone, // wireframe: show every edge, both facings
	});
	// Variant 6 (EPipelineIndex::GizmoUI): tangent-debug color that stamps the nearest depth. A vertex
	// shader override (FORCE_NEAR_DEPTH) forces gl_Position.z = 0 (NDC near) without touching x/y/w, so
	// the shape is unchanged; with depth test+write on it passes eLess against any scene depth (drawn on
	// top of everything) and writes 0.0 so nothing drawn afterwards beats it. Forcing depth in the vertex
	// shader (not gl_FragDepth) keeps early-Z intact and leaves the IES fragment interface uniform.
	graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
		.vertexShader = ShaderSource{
			.text = graphicsPipelineLayout.vertexShader.text,
			.debugFilePath = graphicsPipelineLayout.vertexShader.debugFilePath,
			.defines = { { "FORCE_NEAR_DEPTH", "1" } },
		},
		.fragmentShader = ShaderSource{
			.text = gizmoVariantText,
			.debugFilePath = gizmoVariantPath,
		},
		.blendEnable = false,
		.depthWrite = true,
		.depthTest = true,
		.cullMode = vk::CullModeFlagBits::eNone, // gizmo: double-sided so it reads from any angle
	});
	// Variant 7 (EPipelineIndex::GizmoWorld): tangent-debug color, depth tested (occluded by geometry),
	// alpha-blended, no depth write (world-space gizmo).
	graphicsPipelineLayout.additionalVariants.push_back(PipelineVariant{
		.fragmentShader = ShaderSource{
			.text = gizmoVariantText,
			.debugFilePath = gizmoVariantPath,
            .defines = { },
		},
		.cullMode = vk::CullModeFlagBits::eNone, // gizmo: double-sided so it reads from any angle
	});

    // VR: every shader in this pipeline selects the per-eye view (u_views[u_viewIndex]) from one push
    // constant, gated behind STEREO. Define it once across all shader sources (and add the range once)
    // rather than per-variant; shaders that don't reference STEREO just ignore the define.
    if (m_stereo)
    {
        graphicsPipelineLayout.pushConstantRanges.push_back(vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, .offset = 0, .size = sizeof(uint32) });

        auto defineStereo = [](ShaderSource& shader) {
            if (!shader.text.empty()) // empty = falls back to the default shader, which already gets it
                shader.defines.push_back({ "STEREO", "1" });
        };
        defineStereo(graphicsPipelineLayout.vertexShader);
        defineStereo(graphicsPipelineLayout.fragmentShader);
        for (PipelineVariant& variant : graphicsPipelineLayout.additionalVariants)
        {
            defineStereo(variant.vertexShader);
            defineStereo(variant.fragmentShader);
        }
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
    // 18 = the set's highest binding number: required for eVariableDescriptorCount. descriptorCount is
    // the fixed device-limit cap; the actual array size is supplied per descriptor set allocation, so
    // texture capacity growth never has to recreate this layout (or the pipeline/DGC state built on it).
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_textures
        .binding = 18,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = maxTextures,
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
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // probe_sh (GI clipmap volume)
        .binding = 10,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    for (uint32 binding = 14; binding <= 17; ++binding) // shadow-ray alpha test: vertices/indices/meshInfos/instances
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{
            .binding = binding,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment
        });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_gbufferDepth (AO bilateral upsample)
        .binding = 12,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_ao (denoised screen-space AO)
        .binding = 13,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_tlas (ray-traced light shadows)
        .binding = 11,
        .descriptorType = vk::DescriptorType::eAccelerationStructureKHR,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment
    });

    // Per-binding flags (parallel to descriptorSetBindings): the AO (13) and TLAS (11) bindings are
    // refreshed after the (cached) draw CB is recorded -> UPDATE_AFTER_BIND; the texture array (18) is
    // variable-count (allocated at the live texture capacity) and only partially written.
    graphicsPipelineLayout.descriptorBindingFlags.resize(descriptorSetBindings.size());
    for (size_t i = 0; i < descriptorSetBindings.size(); ++i)
    {
        if (descriptorSetBindings[i].binding == 11 || descriptorSetBindings[i].binding == 13)
            graphicsPipelineLayout.descriptorBindingFlags[i] = vk::DescriptorBindingFlagBits::eUpdateAfterBind;
        else if (descriptorSetBindings[i].binding == 18)
            graphicsPipelineLayout.descriptorBindingFlags[i] = vk::DescriptorBindingFlagBits::ePartiallyBound | vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
    }
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

void StaticMeshGraphicsPipeline::updateTlasDescriptor(vk::DescriptorSet descriptorSet, vk::AccelerationStructureKHR tlas)
{
    // Same recorded-once CB situation as the AO image: the TLAS is double-buffered and rebuilt every frame
    // (its handle can change on resize), so the binding is refreshed per frame via UPDATE_AFTER_BIND.
    vk::WriteDescriptorSetAccelerationStructureKHR asInfo{ .accelerationStructureCount = 1, .pAccelerationStructures = &tlas };
    vk::WriteDescriptorSet write{ .pNext = &asInfo, .dstSet = descriptorSet, .dstBinding = 11, .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eAccelerationStructureKHR };
    Globals::device.getDevice().updateDescriptorSets(1, &write, 0, nullptr);
}

void StaticMeshGraphicsPipeline::initialize(vk::RenderPass renderPass, uint32 maxUniqueMeshes, uint32 maxTextures, bool stereo)
{
    m_renderPass = renderPass;
    m_stereo = stereo;
    m_sampler.initialize();

    GraphicsPipelineLayout graphicsPipelineLayout;
    buildPipelineLayout(graphicsPipelineLayout, maxTextures);
    m_graphicsPipeline.initialize(renderPass, graphicsPipelineLayout);

    m_indirectExecutionSet.initialize(m_graphicsPipeline);
    m_indirectCommandsLayout.initialize(m_graphicsPipeline.getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);

    createPreprocessBuffers(maxUniqueMeshes);
}

void StaticMeshGraphicsPipeline::resizeMeshCapacity(uint32 maxUniqueMeshes)
{
    createPreprocessBuffers(maxUniqueMeshes);
}

void StaticMeshGraphicsPipeline::createPreprocessBuffers(uint32 maxUniqueMeshes)
{
    // Size the preprocess scratch buffer for the worst case (one sequence per unique mesh).
    vk::GeneratedCommandsMemoryRequirementsInfoEXT memReqInfo{
        .indirectExecutionSet = m_indirectExecutionSet.getHandle(),
        .indirectCommandsLayout = m_indirectCommandsLayout.getHandle(),
        .maxSequenceCount = maxUniqueMeshes,
        .maxDrawCount = maxUniqueMeshes,
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
                vk::BufferUsageFlagBits2::ePreprocessBufferEXT | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
            m_transparentPreprocessBuffers[i].initialize(m_preprocessSize,
                vk::BufferUsageFlagBits2::ePreprocessBufferEXT | vk::BufferUsageFlagBits2::eShaderDeviceAddress,
                vk::MemoryPropertyFlagBits::eDeviceLocal);
        }
    }
}

void StaticMeshGraphicsPipeline::reloadShaders(uint32 maxTextures)
{
    if (!m_renderPass)
        return;

    GraphicsPipelineLayout graphicsPipelineLayout;
    buildPipelineLayout(graphicsPipelineLayout, maxTextures);
    if (!m_graphicsPipeline.reloadShaders(m_renderPass, graphicsPipelineLayout))
    {
        printf("StaticMeshGraphicsPipeline: shader reload failed, keeping previous pipeline\n");
        return;
    }

    m_indirectExecutionSet.destroy();
    m_indirectExecutionSet.initialize(m_graphicsPipeline);
}

void StaticMeshGraphicsPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params, bool updateDescriptors)
{
    std::array<DescriptorSetUpdateInfo, 15> graphicsDescriptorSetUpdateInfos
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
            .binding = 14,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo { .buffer = params.vertexBuffer.getBuffer(), .range = params.vertexBuffer.getSize() } }
        },
        DescriptorSetUpdateInfo{
            .binding = 15,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo { .buffer = params.indexBuffer.getBuffer(), .range = params.indexBuffer.getSize() } }
        },
        DescriptorSetUpdateInfo{
            .binding = 16,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo { .buffer = params.meshInfoBuffer.getBuffer(), .range = params.meshInfoBuffer.getSize() } }
        },
        DescriptorSetUpdateInfo{
            .binding = 17,
            .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo { .buffer = params.rtMeshInstancesBuffer.getBuffer(), .range = params.rtMeshInstancesBuffer.getSize() } }
        },
        DescriptorSetUpdateInfo{
            .binding = 12,
            .type = vk::DescriptorType::eCombinedImageSampler,
            .imageInfos = {
                vk::DescriptorImageInfo {
                    .sampler = params.gbufferSampler,
                    .imageView = params.gbufferDepthView,
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            }
        },

        DescriptorSetUpdateInfo{
            .binding = 18,
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
    if (updateDescriptors)
        commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, descriptorSet, graphicsDescriptorSetUpdateInfos);
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
    vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);
    if (m_stereo) // select the eye matrix/view pos for the generated draws (vertex + fragment read it)
        vkCommandBuffer.pushConstants(m_graphicsPipeline.getPipelineLayout(), vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(uint32), &params.viewIndex);
    vkCommandBuffer.bindVertexBuffers(0, { params.vertexBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindVertexBuffers(2, { params.instanceIdxBuffer.getBuffer() }, {0});
    vkCommandBuffer.bindIndexBuffer(params.indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
    recordExecuteGeneratedCommands(vkCommandBuffer, params.indirectCommandBuffer, m_preprocessBuffers[frameIdx], numMeshes);
    recordExecuteGeneratedCommands(vkCommandBuffer, params.transparentIndirectCommandBuffer, m_transparentPreprocessBuffers[frameIdx], numMeshes);
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
