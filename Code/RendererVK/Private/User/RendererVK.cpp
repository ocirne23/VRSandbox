module RendererVK;

import Core;
import Entity.FreeFlyCameraController;
import RendererVK.VK;
import RendererVK.glslang;
import RendererVK.Layout;
import RendererVK.ObjectContainer;
import Core.Window;
import Core.Frustum;
import File.SceneData;
import File.MeshData;
import File.FileSystem;

constexpr static float CAMERA_FOV_DEG = 45.0f;
constexpr static float CAMERA_NEAR = 0.01f;
constexpr static float CAMERA_FAR = 5000.0f;

// TODO make these dynamic
constexpr static uint32 MAX_UNIQUE_MESHES = 100;
constexpr static uint32 MAX_UNIQUE_MATERIALS = 100;
constexpr static uint32 MAX_INSTANCE_DATA = 1024 * 1024;

static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

constexpr static size_t VERTEX_DATA_SIZE = 1024 * 1024 * sizeof(RendererVKLayout::MeshVertex);
constexpr static size_t INDEX_DATA_SIZE = 1024 * 1024 * sizeof(RendererVKLayout::MeshIndex);

constexpr std::array<vk::ClearValue, 2> getClearValues()
{
    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{ std::array<float, 4> { 0.2f, 0.0f, 0.0f, 1.0f } };
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{ 1.0f, 0 };
    return clearValues;
}
constexpr static std::array<vk::ClearValue, 2> s_clearValues = getClearValues();

RendererVK::RendererVK() {}
RendererVK::~RendererVK() {}

bool RendererVK::initialize(Window& window, bool enableValidationLayers)
{
    // Disable layers we don't care about for now to eliminate potential issues
    _putenv("DISABLE_LAYER_NV_OPTIMUS_1=True");
    _putenv("DISABLE_VULKAN_OW_OVERLAY_LAYER=True");
    _putenv("DISABLE_VULKAN_OW_OBS_CAPTURE=True");
    _putenv("DISABLE_VULKAN_OBS_CAPTURE=True");

    glslang::InitializeProcess();

    VK::g_inst.initialize(window, enableValidationLayers);
    VK::g_inst.setBreakOnValidationLayerError(enableValidationLayers);
    VK::g_dev.initialize();
    m_surface.initialize(window);
    assert(m_surface.deviceSupportsSurface());

    m_swapChain.initialize(m_surface, NUM_FRAMES_IN_FLIGHT);
    m_stagingManager.initialize(m_swapChain);
    m_meshDataManager.initialize(m_stagingManager, VERTEX_DATA_SIZE, INDEX_DATA_SIZE);

    m_renderPass.initialize(m_swapChain);
    m_framebuffers.initialize(m_renderPass, m_swapChain);

    m_sampler.initialize();
    m_colorTex.initialize(m_stagingManager, "Textures/boat/color.dds", true);
    m_normalTex.initialize(m_stagingManager, "Textures/boat/normal.dds", false);
    m_rmhTex.initialize(m_stagingManager, "Textures/boat/roughness_metallic_height.dds", false);

    {
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
            .binding = 3,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment
        });
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_normal
            .binding = 4,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment
        });
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // u_roughness_metallic_height
            .binding = 5,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment
        });
        m_graphicsPipeline.initialize(m_renderPass, graphicsPipelineLayout);
    }
    {
        ComputePipelineLayout computePipelineLayout;
        computePipelineLayout.computeShaderText = std::move(FileSystem::readFileStr("Shaders/instanced_indirect.cs.glsl"));
        auto& descriptorSetBindings = computePipelineLayout.descriptorSetLayoutBindings;
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // UBO
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
        });
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInstances
            .binding = 1,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
        });
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // InMeshInfoBuffer
            .binding = 2,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
         });
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutMeshInstanceIndexes
            .binding = 3,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
         });
        descriptorSetBindings.push_back(vk::DescriptorSetLayoutBinding{ // OutIndirectCommandBufer
            .binding = 4,
            .descriptorType = vk::DescriptorType::eStorageBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eVertex
         });
        m_computePipeline.initialize(computePipelineLayout);
    }

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.indirectDispatchBuffer.initialize(sizeof(vk::DispatchIndirectCommand),
            vk::BufferUsageFlagBits::eIndirectBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedDispatchBuffer = (vk::DispatchIndirectCommand*)perFrame.indirectDispatchBuffer.mapMemory().data();

        perFrame.uniformBuffer.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedUniformBuffer = (RendererVKLayout::Ubo*)perFrame.uniformBuffer.mapMemory().data();

        perFrame.computeMeshInfoBuffer.initialize(MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::MeshInfo),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedMeshInfo = (RendererVKLayout::MeshInfo*)perFrame.computeMeshInfoBuffer.mapMemory().data();

        perFrame.instanceDataBuffer.initialize(MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        perFrame.mappedMeshInstances = (RendererVKLayout::MeshInstance*)perFrame.instanceDataBuffer.mapMemory().data();

        perFrame.indirectCommandBuffer.initialize(MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand),
            vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.instanceIdxBuffer.initialize(MAX_INSTANCE_DATA * sizeof(uint32),
            vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
    }

    m_materialInfoBuffer.initialize(MAX_UNIQUE_MATERIALS * sizeof(RendererVKLayout::MaterialInfo),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);


    return true;
}

void RendererVK::update(double deltaSec, const FreeFlyCameraController& camera)
{
    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::mat4x4 projection = glm::perspective(glm::radians(CAMERA_FOV_DEG), (float)extent.width / (float)extent.height, CAMERA_NEAR, CAMERA_FAR);
    glm::mat4 viewMatrix = camera.getViewMatrix();

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    frameData.mappedUniformBuffer[0].mvp = projection * viewMatrix;
    frameData.mappedUniformBuffer[0].frustum.fromMatrix(frameData.mappedUniformBuffer[0].mvp);
    frameData.mappedUniformBuffer[0].viewPos = camera.getPosition();
    
    if (!frameData.updated)
    {
        uint32 instanceCounter = 0;

        for (ObjectContainer* pObjectContainer : m_objectContainers)
        {
            std::vector<RendererVKLayout::MeshInfo>& meshInfos = pObjectContainer->m_meshInfos;
            const uint32 numMeshInfos = (uint32)meshInfos.size();

            for (uint32 i = 0; i < numMeshInfos; ++i)
            {
                meshInfos[i].firstInstance = instanceCounter;
                assert(instanceCounter <= MAX_INSTANCE_DATA);

                const std::vector<RendererVKLayout::MeshInstance>& meshInstances = pObjectContainer->m_meshInstances[i];
                memcpy(&frameData.mappedMeshInstances[instanceCounter], meshInstances.data(), meshInstances.size() * sizeof(RendererVKLayout::MeshInstance));
                instanceCounter += (uint32)meshInstances.size();
            }
            memcpy(&frameData.mappedMeshInfo[pObjectContainer->m_baseMeshInfoIdx], meshInfos.data(), sizeof(RendererVKLayout::MeshInfo) * numMeshInfos);
        }

        m_instanceCounter = instanceCounter;
        frameData.mappedDispatchBuffer[0] = vk::DispatchIndirectCommand{ .x = instanceCounter, .y = 1, .z = 1 };

        frameData.updated = true;
    }
    m_stagingManager.update();
}

void RendererVK::addObjectContainer(ObjectContainer* pObjectContainer)
{
    m_objectContainers.push_back(pObjectContainer);
}

uint32 RendererVK::addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos)
{
    uint32 baseMeshInfoIdx = m_meshInfoCounter;
    m_meshInfoCounter += (uint32)meshInfos.size();
    assert(m_meshInfoCounter < USHRT_MAX);
    assert(m_meshInfoCounter < MAX_UNIQUE_MESHES);
    return baseMeshInfoIdx;
}

uint32 RendererVK::addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos)
{
    uint32 baseMaterialInfoIdx = m_materialInfoCounter;
    m_materialInfoCounter += (uint32)materialInfos.size();
    assert(m_materialInfoCounter < USHRT_MAX);
    assert(m_materialInfoCounter < MAX_UNIQUE_MATERIALS);
    m_stagingManager.upload(m_materialInfoBuffer.getBuffer(), materialInfos.size() * sizeof(RendererVKLayout::MaterialInfo), 
        materialInfos.data(), baseMaterialInfoIdx * sizeof(RendererVKLayout::MaterialInfo));
    return baseMaterialInfoIdx;
}

void RendererVK::recordCommandBuffers()
{
    for (uint32 i = 0; i < NUM_FRAMES_IN_FLIGHT; ++i)
    {
        const vk::Extent2D extent = m_swapChain.getLayout().extent;
        const vk::Viewport viewport{ .x = 0.0f, .y = (float)extent.height, .width = (float)extent.width, .height = -((float)extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
        const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };

        const vk::RenderPassBeginInfo renderPassBeginInfo{
            .renderPass = m_renderPass.getRenderPass(),
            .framebuffer = m_framebuffers.getFramebuffer(i),
            .renderArea = vk::Rect2D {.offset = vk::Offset2D { 0, 0 }, .extent = extent },
            .clearValueCount = (uint32)s_clearValues.size(),
            .pClearValues = s_clearValues.data(),
        };

        PerFrameData& frameData = m_perFrameData[i];
        std::array<DescriptorSetUpdateInfo, 6> graphicsDescriptorSetUpdateInfos
        {
            DescriptorSetUpdateInfo{
                .binding = 0,
                .type = vk::DescriptorType::eUniformBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.uniformBuffer.getBuffer(),
                    .range = sizeof(RendererVKLayout::Ubo),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 1,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.instanceDataBuffer.getBuffer(),
                    .range = MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 2,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = m_materialInfoBuffer.getBuffer(),
                    .range = MAX_UNIQUE_MATERIALS * sizeof(RendererVKLayout::MaterialInfo),
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 3,
                .type = vk::DescriptorType::eCombinedImageSampler,
                .info = vk::DescriptorImageInfo {
                    .sampler = m_sampler.getSampler(),
                    .imageView = m_colorTex.getImageView(),
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 4,
                .type = vk::DescriptorType::eCombinedImageSampler,
                .info = vk::DescriptorImageInfo {
                    .sampler = m_sampler.getSampler(),
                    .imageView = m_normalTex.getImageView(),
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            },
            DescriptorSetUpdateInfo{
                .binding = 5,
                .type = vk::DescriptorType::eCombinedImageSampler,
                .info = vk::DescriptorImageInfo {
                    .sampler = m_sampler.getSampler(),
                    .imageView = m_rmhTex.getImageView(),
                    .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                }
            }
        };

        std::array<DescriptorSetUpdateInfo, 5> computeDescriptorSetUpdateInfos
        {
            DescriptorSetUpdateInfo{ // UBO
                .binding = 0,
                .type = vk::DescriptorType::eUniformBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.uniformBuffer.getBuffer(),
                    .range = sizeof(RendererVKLayout::Ubo),
                }
            },
            DescriptorSetUpdateInfo{ // InMeshInstances
                .binding = 1,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.instanceDataBuffer.getBuffer(),
                    .range = MAX_INSTANCE_DATA * sizeof(RendererVKLayout::MeshInstance),
                }
            },
            DescriptorSetUpdateInfo{ // InMeshInfoBuffer
                .binding = 2,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.computeMeshInfoBuffer.getBuffer(),
                    .range = MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::MeshInfo),
                }
            },
            DescriptorSetUpdateInfo{ // OutMeshInstanceIndexes
                .binding = 3,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.instanceIdxBuffer.getBuffer(),
                    .range = MAX_INSTANCE_DATA * sizeof(uint32),
                }
            },
            DescriptorSetUpdateInfo{ // OutIndirectCommandBufer
                .binding = 4,
                .type = vk::DescriptorType::eStorageBuffer,
                .info = vk::DescriptorBufferInfo {
                    .buffer = frameData.indirectCommandBuffer.getBuffer(),
                    .range = MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand),
                }
            }
        };

        CommandBuffer& commandBuffer = m_swapChain.getCommandBuffer(i);
        vk::CommandBuffer vkCommandBuffer = commandBuffer.begin();

        {   // Compute shader frustum cull and indirect command buffer generation
            vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, m_computePipeline.getPipeline());
            commandBuffer.cmdUpdateDescriptorSets(m_computePipeline.getPipelineLayout(), vk::PipelineBindPoint::eCompute, computeDescriptorSetUpdateInfos);
            vkCommandBuffer.fillBuffer(frameData.indirectCommandBuffer.getBuffer(), 0, MAX_UNIQUE_MESHES * sizeof(vk::DrawIndexedIndirectCommand), 0);
            vk::MemoryBarrier memoryBarrier{ .srcAccessMask = vk::AccessFlagBits::eTransferWrite, .dstAccessMask = vk::AccessFlagBits::eShaderRead };
            vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlags::Flags(0), { memoryBarrier }, {}, {});
            vkCommandBuffer.dispatchIndirect(frameData.indirectDispatchBuffer.getBuffer(), 0);
            vk::MemoryBarrier memoryBarrier2{ .srcAccessMask = vk::AccessFlagBits::eShaderWrite, .dstAccessMask = vk::AccessFlagBits::eIndirectCommandRead };
            vkCommandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eDrawIndirect, vk::DependencyFlags::Flags(0), { memoryBarrier2 }, {}, {});
        }
        {   // Graphics pipeline draw
            vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
            commandBuffer.cmdUpdateDescriptorSets(m_graphicsPipeline.getPipelineLayout(), vk::PipelineBindPoint::eGraphics, graphicsDescriptorSetUpdateInfos);
            vkCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphicsPipeline.getPipeline());
            vkCommandBuffer.setViewport(0, { viewport });
            vkCommandBuffer.setScissor(0, { scissor });
            vkCommandBuffer.bindVertexBuffers(0, { m_meshDataManager.getVertexBuffer().getBuffer() }, { 0 });
            vkCommandBuffer.bindVertexBuffers(2, { frameData.instanceIdxBuffer.getBuffer() }, { 0 });
            vkCommandBuffer.bindIndexBuffer(m_meshDataManager.getIndexBuffer().getBuffer(), 0, vk::IndexType::eUint32);
            vkCommandBuffer.drawIndexedIndirect(frameData.indirectCommandBuffer.getBuffer(), 0, m_meshInfoCounter, sizeof(vk::DrawIndexedIndirectCommand));
            vkCommandBuffer.endRenderPass();
        }
        commandBuffer.end();
    }
}

void RendererVK::render()
{
    m_swapChain.acquireNextImage();
    m_swapChain.present();
}

const char* RendererVK::getDebugText()
{
    float vertexDataPercent = (float)(m_meshDataManager.getVertexBufUsed() / (float)m_meshDataManager.getVertexBufSize() * 100.0f);
    float indexDataPercent = (float)(m_meshDataManager.getIndexBufUsed() / (float)m_meshDataManager.getIndexBufSize()) * 100.0f;
    static char buffer[256];
    snprintf(buffer, sizeof(buffer), "Vertex Capacity: %0.1f%%, Index Capacity: %0.1f%%", vertexDataPercent, indexDataPercent);
    return buffer;
}