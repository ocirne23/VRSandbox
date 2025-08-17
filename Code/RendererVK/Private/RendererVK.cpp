module RendererVK;

import Core;
import Core.glm;
import Core.Window;
import Core.Frustum;

import File.SceneData;
import File.MeshData;
import File.FileSystem;

import RendererVK.RenderNode;
import RendererVK.VK;
import RendererVK.glslang;
import RendererVK.Layout;
import RendererVK.ObjectContainer;
import RendererVK.Camera;

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
        perFrame.ubo.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits::eUniformBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedUniformBuffer = (RendererVKLayout::Ubo*)perFrame.ubo.mapMemory().data();

        perFrame.inRenderNodeTransformsBuffer.initialize(RendererVKLayout::MAX_RENDER_NODES * sizeof(RendererVKLayout::RenderNodeTransform),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedRenderNodeTransforms = perFrame.inRenderNodeTransformsBuffer.mapMemory<RendererVKLayout::RenderNodeTransform>();

        perFrame.inMeshInstancesBuffer.initialize(RendererVKLayout::MAX_INSTANCE_DATA * sizeof(RendererVKLayout::InMeshInstance),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedMeshInstances = perFrame.inMeshInstancesBuffer.mapMemory<RendererVKLayout::InMeshInstance>();

        perFrame.inFirstInstancesBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(uint32),
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedFirstInstances = perFrame.inFirstInstancesBuffer.mapMemory<uint32>();
    }

    m_meshInfosBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MESHES * sizeof(RendererVKLayout::MeshInfo),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    m_materialInfosBuffer.initialize(RendererVKLayout::MAX_UNIQUE_MATERIALS * sizeof(RendererVKLayout::MaterialInfo),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    m_instanceOffsetsBuffer.initialize(RendererVKLayout::MAX_INSTANCE_OFFSETS * sizeof(RendererVKLayout::MeshInstanceOffset),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal);

    return true;
}

const Frustum& RendererVK::beginFrame(const Camera& camera)
{
    m_meshInstanceCounter = 0;
    memset(m_numInstancesPerMesh.data(), 0, m_numInstancesPerMesh.size() * sizeof(m_numInstancesPerMesh[0]));

    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::mat4x4 projection = glm::perspective(glm::radians(camera.fovDeg), (float)extent.width / (float)extent.height, camera.near, camera.far);
    glm::mat4 viewMatrix = camera.viewMatrix;

    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    frameData.mappedUniformBuffer->mvp = projection * viewMatrix;
    frameData.mappedUniformBuffer->frustum.fromMatrix(frameData.mappedUniformBuffer->mvp);
    frameData.mappedUniformBuffer->viewPos = camera.position;

    return frameData.mappedUniformBuffer->frustum;
}

void RendererVK::renderNode(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    assert(m_meshInstanceCounter + numInstances <= RendererVKLayout::MAX_INSTANCE_DATA);

    for (auto& pair : node.m_numInstancesPerMesh)
        m_numInstancesPerMesh[pair.first] += pair.second;

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    memcpy(frameData.mappedMeshInstances.data() + m_meshInstanceCounter, node.m_meshInstances.data(), numInstances * sizeof(node.m_meshInstances[0]));

    m_meshInstanceCounter += numInstances;
}

void RendererVK::present(const Camera& camera)
{
    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    assert(frameData.mappedRenderNodeTransforms.size() >= m_renderNodeTransforms.size());
    assert(frameData.mappedMeshInstances.size() >= m_meshInstanceCounter);
    assert(frameData.mappedFirstInstances.size() >= m_meshInfoCounter);

    memcpy(frameData.mappedRenderNodeTransforms.data(), m_renderNodeTransforms.data(), m_renderNodeTransforms.size() * sizeof(m_renderNodeTransforms[0]));

    uint32 instanceCounter = 0;
    const uint32 numMeshInfos = (uint32)m_numInstancesPerMesh.size();
    for (uint32 meshIdx = 0; meshIdx < numMeshInfos; ++meshIdx)
    {
        frameData.mappedFirstInstances[meshIdx] = instanceCounter;
        instanceCounter += m_numInstancesPerMesh[meshIdx];
    }

    const static vk::DeviceSize atomSize = Globals::device.getNonCoherentAtomSize();
    vk::DeviceSize flushSize = m_renderNodeTransforms.size() * sizeof(m_renderNodeTransforms[0]);
    flushSize = (flushSize + atomSize - 1) & ~(atomSize - 1);

    Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inRenderNodeTransformsBuffer.getMemory(), .offset = 0, .size = flushSize
    } });

    flushSize = m_meshInstanceCounter * sizeof(RendererVKLayout::InMeshInstance);
    flushSize = (flushSize + atomSize - 1) & ~(atomSize - 1);
    Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inMeshInstancesBuffer.getMemory(), .offset = 0, .size = flushSize
    } });

    flushSize = numMeshInfos * sizeof(uint32);
    flushSize = (flushSize + atomSize - 1) & ~(atomSize - 1);
    Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inFirstInstancesBuffer.getMemory(), .offset = 0, .size = flushSize
    } });

    Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.ubo.getMemory(), .offset = 0, .size = vk::WholeSize
    } });

    m_indirectCullComputePipeline.update(frameIdx, m_meshInstanceCounter);
    m_stagingManager.update();

    recordCommandBuffers();

    m_swapChain.acquireNextImage();
    m_swapChain.present();
}

void RendererVK::addObjectContainer(ObjectContainer* pObjectContainer)
{
    m_objectContainers.push_back(pObjectContainer);
}

uint32 RendererVK::addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos)
{
    const uint32 baseMeshInfoIdx = m_meshInfoCounter;
    m_meshInfoCounter += (uint32)meshInfos.size();
    m_numInstancesPerMesh.resize(m_meshInfoCounter);

    assert(m_meshInfoCounter < USHRT_MAX);
    assert(m_meshInfoCounter < RendererVKLayout::MAX_UNIQUE_MESHES);

    m_stagingManager.upload(m_meshInfosBuffer.getBuffer(), meshInfos.size() * sizeof(RendererVKLayout::MeshInfo),
        meshInfos.data(), baseMeshInfoIdx * sizeof(RendererVKLayout::MeshInfo));

    setHaveToRecordCommandBuffers();
    return baseMeshInfoIdx;
}

uint32 RendererVK::addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos)
{
    const uint32 baseMaterialInfoIdx = m_materialInfoCounter;
    m_materialInfoCounter += (uint32)materialInfos.size();
    assert(m_materialInfoCounter < USHRT_MAX);
    assert(m_materialInfoCounter < RendererVKLayout::MAX_UNIQUE_MATERIALS);

    m_stagingManager.upload(m_materialInfosBuffer.getBuffer(), materialInfos.size() * sizeof(RendererVKLayout::MaterialInfo),
        materialInfos.data(), baseMaterialInfoIdx * sizeof(RendererVKLayout::MaterialInfo));

    setHaveToRecordCommandBuffers();
    return baseMaterialInfoIdx;
}

uint32 RendererVK::addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets)
{
    const uint32 baseInstanceOffsetIdx = m_instanceOffsetCounter;
    m_instanceOffsetCounter += (uint32)meshInstanceOffsets.size();
    assert(m_instanceOffsetCounter < RendererVKLayout::MAX_INSTANCE_OFFSETS);

    m_stagingManager.upload(m_instanceOffsetsBuffer.getBuffer(), meshInstanceOffsets.size() * sizeof(RendererVKLayout::MeshInstanceOffset),
        meshInstanceOffsets.data(), baseInstanceOffsetIdx * sizeof(RendererVKLayout::MeshInstanceOffset));

    setHaveToRecordCommandBuffers();
    return baseInstanceOffsetIdx;
}

uint32 RendererVK::addRenderNodeTransform(const Transform& transform)
{
    const uint32 renderNodeIdx = (uint32)m_renderNodeTransforms.size();
    m_renderNodeTransforms.emplace_back(transform);
    return renderNodeIdx;
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

        IndirectCullComputePipeline::RecordParams cullParams
        {
            .ubo = frameData.ubo,
            .inRenderNodeTransformsBuffer = frameData.inRenderNodeTransformsBuffer,
            .inMeshInstancesBuffer = frameData.inMeshInstancesBuffer,
            .inMeshInstanceOffsetsBuffer = m_instanceOffsetsBuffer,
            .inMeshInfoBuffer = m_meshInfosBuffer,
            .inFirstInstancesBuffer = frameData.inFirstInstancesBuffer
        };
        m_indirectCullComputePipeline.record(commandBuffer, frameIdx, m_meshInfoCounter, cullParams);

        vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
        vkCommandBuffer.setViewport(0, { viewport });
        vkCommandBuffer.setScissor(0, { scissor });

        StaticMeshGraphicsPipeline::RecordParams drawParams
        {
            .ubo = frameData.ubo,
            .vertexBuffer = m_meshDataManager.getVertexBuffer(),
            .indexBuffer = m_meshDataManager.getIndexBuffer(),
            .materialInfoBuffer = m_materialInfosBuffer,
            .instanceIdxBuffer = m_indirectCullComputePipeline.getInstanceIdxBuffer(frameIdx),
            .meshInstanceBuffer = m_indirectCullComputePipeline.getOutMeshInstancesBuffer(frameIdx),
            .indirectCommandBuffer = m_indirectCullComputePipeline.getIndirectCommandBuffer(frameIdx)
        };

        m_staticMeshGraphicsPipeline.record(commandBuffer, frameIdx, m_meshInfoCounter, drawParams);
        vkCommandBuffer.endRenderPass();

        commandBuffer.end();

        frameData.updated = true;
    }
}

void RendererVK::setHaveToRecordCommandBuffers()
{
    for (PerFrameData& perFrame : m_perFrameData)
        perFrame.updated = false;
}