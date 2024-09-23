module RendererVK;

import Core;
import Core.glm;
import Core.Window;
import Core.Frustum;

import Entity.FreeFlyCameraController;

import File.SceneData;
import File.MeshData;
import File.FileSystem;

import RendererVK.VK;
import RendererVK.glslang;
import RendererVK.Layout;
import RendererVK.ObjectContainer;

constexpr static float CAMERA_FOV_DEG = 45.0f;
constexpr static float CAMERA_NEAR = 0.01f;
constexpr static float CAMERA_FAR = 5000.0f;

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

    Globals::instance.initialize(window, enableValidationLayers);
    Globals::instance.setBreakOnValidationLayerError(enableValidationLayers);
    Globals::device.initialize();
    m_surface.initialize(window);
    assert(m_surface.deviceSupportsSurface());

    m_swapChain.initialize(m_surface, RendererVKLayout::NUM_FRAMES_IN_FLIGHT);
    m_stagingManager.initialize(m_swapChain);
    m_meshDataManager.initialize(m_stagingManager, VERTEX_DATA_SIZE, INDEX_DATA_SIZE);

    m_renderPass.initialize(m_swapChain);
    m_framebuffers.initialize(m_renderPass, m_swapChain);

    m_staticMeshGraphicsPipeline.initialize(m_renderPass, m_stagingManager);
    m_indirectCullComputePipeline.initialize();

    vk::Device vkDevice = Globals::device.getDevice();

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.uniformBuffer.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedUniformBuffer = (RendererVKLayout::Ubo*)perFrame.uniformBuffer.mapMemory().data();
    }

    m_materialInfoBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MATERIALS * sizeof(RendererVKLayout::MaterialInfo),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    return true;
}

void RendererVK::update(double deltaSec, const FreeFlyCameraController& camera)
{
    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::mat4x4 projection = glm::perspective(glm::radians(CAMERA_FOV_DEG), (float)extent.width / (float)extent.height, CAMERA_NEAR, CAMERA_FAR);
    glm::mat4 viewMatrix = camera.getViewMatrix();

    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    frameData.mappedUniformBuffer[0].mvp = projection * viewMatrix;
    frameData.mappedUniformBuffer[0].frustum.fromMatrix(frameData.mappedUniformBuffer[0].mvp);
    frameData.mappedUniformBuffer[0].viewPos = camera.getPosition();

    Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.uniformBuffer.getMemory(), .offset = 0, .size = vk::WholeSize
    }});
    
    m_indirectCullComputePipeline.update(frameIdx, m_objectContainers);
    m_instanceCounter = m_indirectCullComputePipeline.getInstanceCounter(frameIdx);
    m_stagingManager.update();

    recordCommandBuffers();
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
    assert(m_meshInfoCounter < RendererVKLayout::MAX_UNIQUE_MESHES);
    setHaveToRecordCommandBuffers();
    return baseMeshInfoIdx;
}

uint32 RendererVK::addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos)
{
    uint32 baseMaterialInfoIdx = m_materialInfoCounter;
    m_materialInfoCounter += (uint32)materialInfos.size();
    assert(m_materialInfoCounter < USHRT_MAX);
    assert(m_materialInfoCounter < RendererVKLayout::MAX_UNIQUE_MATERIALS);
    m_stagingManager.upload(m_materialInfoBuffer.getBuffer(), materialInfos.size() * sizeof(RendererVKLayout::MaterialInfo), 
        materialInfos.data(), baseMaterialInfoIdx * sizeof(RendererVKLayout::MaterialInfo));
    setHaveToRecordCommandBuffers();
    return baseMaterialInfoIdx;
}

void RendererVK::recordCommandBuffers()
{
    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    CommandBuffer& commandBuffer = m_swapChain.getCommandBuffer(frameIdx);
    if (!frameData.updated)
    {
        const vk::Extent2D extent = m_swapChain.getLayout().extent;
        const vk::Viewport viewport{ .x = 0.0f, .y = (float)extent.height, .width = (float)extent.width, .height = -((float)extent.height), .minDepth = 0.0f, .maxDepth = 1.0f };
        const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };

        const vk::RenderPassBeginInfo renderPassBeginInfo{
            .renderPass = m_renderPass.getRenderPass(),
            .framebuffer = m_framebuffers.getFramebuffer(frameIdx),
            .renderArea = vk::Rect2D {.offset = vk::Offset2D { 0, 0 }, .extent = extent },
            .clearValueCount = (uint32)s_clearValues.size(),
            .pClearValues = s_clearValues.data(),
        };

        m_swapChain.waitForCurrentCommandBuffer();

        vk::CommandBuffer vkCommandBuffer = commandBuffer.begin();

        m_indirectCullComputePipeline.record(commandBuffer, frameIdx, frameData.uniformBuffer);

        vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        vkCommandBuffer.setViewport(0, { viewport });
        vkCommandBuffer.setScissor(0, { scissor });
        m_staticMeshGraphicsPipeline.record(commandBuffer, frameIdx, frameData.uniformBuffer, m_materialInfoBuffer, m_indirectCullComputePipeline, m_meshDataManager);
        vkCommandBuffer.endRenderPass();

        commandBuffer.end();

        frameData.updated = true;
    }
}

void RendererVK::render()
{
    m_swapChain.acquireNextImage();
    m_swapChain.present();
}

void RendererVK::setHaveToRecordCommandBuffers()
{
    for (PerFrameData& perFrame : m_perFrameData)
        perFrame.updated = false;
}