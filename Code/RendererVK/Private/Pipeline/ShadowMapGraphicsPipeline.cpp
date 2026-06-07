module RendererVK:ShadowMapGraphicsPipeline;

import Core;
import File.FileSystem;

import :CommandBuffer;
import :Device;
import :Layout;
import :Texture;
import :TextureManager;

ShadowMapGraphicsPipeline::ShadowMapGraphicsPipeline() {}
ShadowMapGraphicsPipeline::~ShadowMapGraphicsPipeline() {}

void ShadowMapGraphicsPipeline::buildPipelineLayout(GraphicsPipelineLayout& layout)
{
    layout.vertexShader.debugFilePath = "Shaders/shadow_depth.vs.glsl";
    layout.vertexShader.text = FileSystem::readFileStr(layout.vertexShader.debugFilePath);
    // Fragment stage discards alpha-masked (cutout) fragments so foliage casts correct shadows.
    layout.fragmentShader.debugFilePath = "Shaders/shadow_depth.fs.glsl";
    layout.fragmentShader.text = FileSystem::readFileStr(layout.fragmentShader.debugFilePath);

    layout.depthOnly = true;
    layout.depthBiasEnable = true;
    layout.depthBiasConstantFactor = 1.25f; // slope-scaled bias fights shadow acne on lit slopes
    layout.depthBiasSlopeFactor = 2.5f;
    layout.cullMode = vk::CullModeFlagBits::eFront;

    auto& bindings = layout.vertexLayoutInfo.bindingDescriptions;
    bindings.push_back(vk::VertexInputBindingDescription{ .binding = 0, .stride = sizeof(RendererVKLayout::MeshVertex), .inputRate = vk::VertexInputRate::eVertex });
    bindings.push_back(vk::VertexInputBindingDescription{ .binding = 2, .stride = sizeof(uint32), .inputRate = vk::VertexInputRate::eInstance });

    auto& attributes = layout.vertexLayoutInfo.attributeDescriptions;
    attributes.push_back(vk::VertexInputAttributeDescription{ .location = 0, .binding = 0, .format = vk::Format::eR32G32B32Sfloat, .offset = offsetof(RendererVKLayout::MeshVertex, position) });
    attributes.push_back(vk::VertexInputAttributeDescription{ .location = 3, .binding = 0, .format = vk::Format::eR32G32Sfloat, .offset = offsetof(RendererVKLayout::MeshVertex, texCoord) });
    attributes.push_back(vk::VertexInputAttributeDescription{ .location = 4, .binding = 2, .format = vk::Format::eR32Uint, .offset = 0 });

    auto& descriptorSetBindings = layout.descriptorSetLayoutBindings;
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // main UBO (cascade view-projs)
        .binding = 0, .descriptorType = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // shared transformed instances
        .binding = 1, .descriptorType = vk::DescriptorType::eStorageBuffer, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eVertex });
    descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // diffuse texture array (alpha-mask discard)
        .binding = 7, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = RendererVKLayout::MAX_TEXTURES, .stageFlags = vk::ShaderStageFlagBits::eFragment });
    // The cascade is selected per multiview view via gl_ViewIndex, so no push constant is needed.
}

void ShadowMapGraphicsPipeline::buildIndirectState()
{
    // The shadow pass renders all cascades in a single multiview render pass (non-zero viewMask), where DGC
    // forbids an Indirect Execution Set. We only ever use the one depth-only pipeline anyway, so the commands
    // layout omits the EXECUTION_SET token and we bind the pipeline explicitly + pass a null execution set.
    m_indirectCommandsLayout.initialize(m_graphicsPipeline.getPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, /*useExecutionSet*/ false);

    // With no execution set, DGC needs the pipeline it will generate draws for supplied via pNext.
    vk::GeneratedCommandsPipelineInfoEXT pipelineInfo{ .pipeline = m_graphicsPipeline.getPipeline() };
    vk::GeneratedCommandsMemoryRequirementsInfoEXT memReqInfo{
        .pNext = &pipelineInfo,
        .indirectExecutionSet = VK_NULL_HANDLE,
        .indirectCommandsLayout = m_indirectCommandsLayout.getHandle(),
        .maxSequenceCount = RendererVKLayout::MAX_UNIQUE_MESHES,
        .maxDrawCount = RendererVKLayout::MAX_UNIQUE_MESHES,
    };
    vk::MemoryRequirements2 memReq;
    Globals::device.getDevice().getGeneratedCommandsMemoryRequirementsEXT(&memReqInfo, &memReq);
    m_preprocessSize = memReq.memoryRequirements.size;
    if (m_preprocessSize > 0)
    {
        for (Buffer& preprocess : m_preprocessBuffers)
            preprocess.initialize(m_preprocessSize, {}, vk::MemoryPropertyFlagBits::eDeviceLocal,
                vk::BufferUsageFlagBits2::ePreprocessBufferEXT | vk::BufferUsageFlagBits2::eShaderDeviceAddress);
    }
}

void ShadowMapGraphicsPipeline::initialize(ShadowMap& shadowMap)
{
    m_renderPass = shadowMap.getRenderPass();
    m_sampler.initialize();
    GraphicsPipelineLayout layout;
    buildPipelineLayout(layout);
    m_graphicsPipeline.initialize(m_renderPass, layout);
    buildIndirectState();
}

void ShadowMapGraphicsPipeline::reloadShaders()
{
    if (!m_renderPass)
        return;
    GraphicsPipelineLayout layout;
    buildPipelineLayout(layout);
    if (!m_graphicsPipeline.reloadShaders(m_renderPass, layout))
    {
        printf("ShadowMapGraphicsPipeline: shader reload failed, keeping previous pipeline\n");
        return;
    }
    // No execution set to rebuild: the multiview shadow pass binds its single pipeline directly.
}

void ShadowMapGraphicsPipeline::record(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 numMeshes, RecordParams& params)
{
    std::array<DescriptorSetUpdateInfo, 3> updates{
        DescriptorSetUpdateInfo{ .binding = 0, .type = vk::DescriptorType::eUniformBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.ubo.getBuffer(), .range = params.ubo.getSize() } } },
        DescriptorSetUpdateInfo{ .binding = 1, .type = vk::DescriptorType::eStorageBuffer,
            .bufferInfos = { vk::DescriptorBufferInfo{ .buffer = params.meshInstanceBuffer.getBuffer(), .range = params.meshInstanceBuffer.getSize() } } },
        DescriptorSetUpdateInfo{ .binding = 7, .type = vk::DescriptorType::eCombinedImageSampler },
    };
    const std::vector<Texture>& textures = Globals::textureManager.getTextures();
    for (const Texture& tex : textures)
    {
        updates[2].imageInfos.push_back(vk::DescriptorImageInfo{
            .sampler = m_sampler.getSampler(),
            .imageView = tex.getImageView(),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        });
    }

    vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
    vk::DescriptorSet descriptorSet = params.descriptorSet.getDescriptorSet();
    commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, descriptorSet, updates);
    vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
    vkCommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipelineLayout(), 0, 1, &descriptorSet, 0, nullptr);
    vkCommandBuffer.bindVertexBuffers(0, { params.vertexBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindVertexBuffers(2, { params.instanceIdxBuffer.getBuffer() }, { 0 });
    vkCommandBuffer.bindIndexBuffer(params.indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);

    // One multiview execute renders all cascades (gl_ViewIndex selects the layer/matrix).
    // With no execution set, the target pipeline must be supplied via pNext (matches the memory-requirements query).
    vk::GeneratedCommandsPipelineInfoEXT pipelineInfo{ .pipeline = m_graphicsPipeline.getPipeline() };
    vk::GeneratedCommandsInfoEXT generatedCommandsInfo{
        .pNext = &pipelineInfo,
        .shaderStages = vk::ShaderStageFlagBits::eVertex,
        .indirectExecutionSet = VK_NULL_HANDLE, // single pipeline, bound explicitly above (multiview disallows an execution set)
        .indirectCommandsLayout = m_indirectCommandsLayout.getHandle(),
        .indirectAddress = params.indirectCommandBuffer.getDeviceAddress(),
        .indirectAddressSize = numMeshes * sizeof(RendererVKLayout::IndirectDrawSequence),
        .preprocessAddress = m_preprocessSize > 0 ? m_preprocessBuffers[frameIdx].getDeviceAddress() : 0,
        .preprocessSize = m_preprocessSize,
        .maxSequenceCount = numMeshes,
        .sequenceCountAddress = 0,
        .maxDrawCount = numMeshes,
    };
    vkCommandBuffer.executeGeneratedCommandsEXT(vk::False, generatedCommandsInfo);
}
