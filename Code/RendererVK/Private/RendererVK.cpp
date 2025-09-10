module RendererVK;

import Core;
import Core.glm;
import Core.Window;
import Core.Frustum;
import Core.imgui;

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

RendererVK::~RendererVK() 
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK destructor");
    }
    ImGui_ImplVulkan_Shutdown();
}

bool RendererVK::initialize(Window& window, EValidation validation, EVSync vsync)
{
    // Disable layers we don't care about for now to eliminate potential issues
    _putenv("DISABLE_LAYER_NV_OPTIMUS_1=True");
    _putenv("DISABLE_VULKAN_OW_OVERLAY_LAYER=True");
    _putenv("DISABLE_VULKAN_OW_OBS_CAPTURE=True");
    _putenv("DISABLE_VULKAN_OBS_CAPTURE=True");

    glslang::InitializeProcess();
    const bool enableValidationLayers = (validation == EValidation::ENABLED);
    m_vsyncEnabled = (vsync == EVSync::ENABLED);
    Globals::instance.initialize(window, enableValidationLayers);
    Globals::instance.setBreakOnValidationLayerError(enableValidationLayers);
    Globals::device.initialize();

    window.getWindowSize(m_windowSize);
    m_surface.initialize(window);
    assert(m_surface.deviceSupportsSurface());

    m_swapChain.initialize(m_surface, RendererVKLayout::NUM_FRAMES_IN_FLIGHT, m_vsyncEnabled);
    m_stagingManager.initialize();
    m_meshDataManager.initialize(m_stagingManager, VERTEX_DATA_SIZE, INDEX_DATA_SIZE);

    m_renderPass.initialize(m_swapChain);
    m_framebuffers.initialize(m_renderPass, m_swapChain);

    m_staticMeshGraphicsPipeline.initialize(m_renderPass, m_stagingManager);
    m_indirectCullComputePipeline.initialize();

    vk::Device vkDevice = Globals::device.getDevice();

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.primaryCommandBuffer.initialize(vk::CommandBufferLevel::ePrimary);
        perFrame.indirectCullCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.staticMeshRenderCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.imguiCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);

        perFrame.ubo.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);
        //perFrame.mappedUniformBuffer = (RendererVKLayout::Ubo*)perFrame.ubo.mapMemory().data();

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


    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    ImGui_ImplSDL3_InitForVulkan((SDL_Window*)window.getWindowHandle());
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.ApiVersion = Globals::instance.getApiVersion();
    init_info.Instance = Globals::instance.getInstance();
    init_info.PhysicalDevice = Globals::device.getPhysicalDevice();
    init_info.Device = Globals::device.getDevice();
    init_info.QueueFamily = Globals::device.getGraphicsQueueIndex();
    init_info.Queue = Globals::device.getGraphicsQueue();
    init_info.PipelineCache = nullptr;
    init_info.RenderPass = m_renderPass.getRenderPass();
    init_info.DescriptorPool = nullptr;
    init_info.DescriptorPoolSize = 10;
    init_info.Subpass = 0;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = imgui_check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplVulkan_MainPipelineCreateInfo pipeline_create;
    pipeline_create.RenderPass = m_renderPass.getRenderPass();
    pipeline_create.Subpass = 0;
    pipeline_create.MSAASamples = {};
    ImGui_ImplVulkan_CreateMainPipeline(pipeline_create);

    m_viewportRect.max = glm::ivec2(m_swapChain.getLayout().extent.width, m_swapChain.getLayout().extent.height);

    return true;
}

void RendererVK::recreateWindowSurface(Window& window)
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK::recreateWindowSurface");
    }

    window.getWindowSize(m_windowSize);
    m_swapChain.destroy();
    m_surface.initialize(window);
    m_swapChain.initialize(m_surface, RendererVKLayout::NUM_FRAMES_IN_FLIGHT, m_vsyncEnabled);
    m_framebuffers.initialize(m_renderPass, m_swapChain);
    auto waitResult2 = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult2 != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK::recreateWindowSurface");
    }
}

void RendererVK::recreateSwapchain()
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK::recreateSwapchain");
    }
    printf("recreateSwapchain()\n");
    m_swapChain.initialize(m_surface, RendererVKLayout::NUM_FRAMES_IN_FLIGHT, m_vsyncEnabled);
    m_framebuffers.initialize(m_renderPass, m_swapChain);
}

void RendererVK::setWindowMinimized(bool minimized)
{
    m_windowMinimized = minimized;
}

const Frustum& RendererVK::beginFrame(const Camera& camera)
{
    m_meshInstanceCounter = 0;
    memset(m_numInstancesPerMesh.data(), 0, m_numInstancesPerMesh.size() * sizeof(m_numInstancesPerMesh[0]));

    const glm::ivec2 viewportSize = m_viewportRect.getSize();
    const glm::mat4x4 projection = glm::perspective(glm::radians(camera.fovDeg), (float)viewportSize.x / (float)viewportSize.y, camera.near, camera.far);
    glm::mat4 viewMatrix = camera.viewMatrix;

    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    static RendererVKLayout::Ubo ubo;
    ubo.mvp = projection * viewMatrix;
    ubo.frustum.fromMatrix(ubo.mvp);
    ubo.viewPos = camera.position; 
    m_stagingManager.upload(frameData.ubo.getBuffer(), sizeof(RendererVKLayout::Ubo), &ubo);

    //frameData.mappedUniformBuffer->mvp = projection * viewMatrix;
    //frameData.mappedUniformBuffer->frustum.fromMatrix(frameData.mappedUniformBuffer->mvp);
    //frameData.mappedUniformBuffer->viewPos = camera.position;

    return ubo.frustum;
}

void RendererVK::renderNodeThreadSafe(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    const uint32 startIdx = std::atomic_ref<uint32>(m_meshInstanceCounter).fetch_add(numInstances);
    assert(startIdx + numInstances <= RendererVKLayout::MAX_INSTANCE_DATA);

    for (auto& pair : node.m_numInstancesPerMesh)
        std::atomic_ref<uint32>(m_numInstancesPerMesh[pair.first]) += pair.second;

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    memcpy(frameData.mappedMeshInstances.data() + startIdx, node.m_meshInstances.data(), numInstances * sizeof(node.m_meshInstances[0]));
}

void RendererVK::renderNode(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    const uint32 startIdx = m_meshInstanceCounter;
    m_meshInstanceCounter += numInstances;
    assert(m_meshInstanceCounter <= RendererVKLayout::MAX_INSTANCE_DATA);

    for (auto& pair : node.m_numInstancesPerMesh)
        m_numInstancesPerMesh[pair.first] += pair.second;

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    //m_stagingManager.upload(frameData.inMeshInstancesBuffer.getBuffer(), )
    memcpy(frameData.mappedMeshInstances.data() + startIdx, node.m_meshInstances.data(), numInstances * sizeof(node.m_meshInstances[0]));
}

void RendererVK::present()
{
    if(m_windowMinimized)
        return;

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

    auto flushRenderNodeTransformsResult = Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inRenderNodeTransformsBuffer.getMemory(), .offset = 0, .size = flushSize
    } });
    assert(flushRenderNodeTransformsResult == vk::Result::eSuccess && "Failed to flush render node transforms memory range");

    flushSize = m_meshInstanceCounter * sizeof(RendererVKLayout::InMeshInstance);
    flushSize = (flushSize + atomSize - 1) & ~(atomSize - 1);
    auto flushMeshInstancesResult = Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inMeshInstancesBuffer.getMemory(), .offset = 0, .size = flushSize
    } });
    assert(flushMeshInstancesResult == vk::Result::eSuccess && "Failed to flush mesh instances memory range");

    flushSize = numMeshInfos * sizeof(uint32);
    flushSize = (flushSize + atomSize - 1) & ~(atomSize - 1);
    auto flushFirstInstancesResult = Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
        .memory = frameData.inFirstInstancesBuffer.getMemory(), .offset = 0, .size = flushSize
    } });
    assert(flushFirstInstancesResult == vk::Result::eSuccess && "Failed to flush first instances memory range");

    //auto flushUBOResult = Globals::device.getDevice().flushMappedMemoryRanges({ vk::MappedMemoryRange{
    //    .memory = frameData.ubo.getMemory(), .offset = 0, .size = vk::WholeSize
    //} });
    //assert(flushUBOResult == vk::Result::eSuccess && "Failed to flush UBO memory range");

    (void)flushRenderNodeTransformsResult; (void)flushMeshInstancesResult; (void)flushFirstInstancesResult; //(void)flushUBOResult;

    m_indirectCullComputePipeline.update(frameIdx, m_meshInstanceCounter);
    m_stagingManager.update();

    if (!m_swapChain.acquireNextImage())
    {
        recreateSwapchain();
        return;
    }
    recordCommandBuffers();

    m_swapChain.submitCommandBuffer(getCurrentCommandBuffer());
    if (!m_swapChain.present())
    {
        recreateSwapchain();
    }
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

    vk::CommandBufferInheritanceInfo inheritanceInfo
    {
        .renderPass = m_renderPass.getRenderPass(),
        .subpass = 0,
        .framebuffer = VK_NULL_HANDLE,// m_framebuffers.getFramebuffer(m_swapChain.getCurrentImageIdx()),
        .occlusionQueryEnable = false,
        .queryFlags = {},
        .pipelineStatistics = {}
    };

    //if (frameData.primaryCommandBuffer.hasRecorded())
    //   m_swapChain.waitForFrame(frameIdx);

    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    if (!frameData.updated)
    {
        {
            CommandBuffer& indirectCullCommandBuffer = frameData.indirectCullCommandBuffer;
            vk::CommandBufferInheritanceInfo indirectInheritanceInfo;
            indirectCullCommandBuffer.begin(false, &indirectInheritanceInfo);
            IndirectCullComputePipeline::RecordParams cullParams
            {
                .ubo = frameData.ubo,
                .inRenderNodeTransformsBuffer = frameData.inRenderNodeTransformsBuffer,
                .inMeshInstancesBuffer = frameData.inMeshInstancesBuffer,
                .inMeshInstanceOffsetsBuffer = m_instanceOffsetsBuffer,
                .inMeshInfoBuffer = m_meshInfosBuffer,
                .inFirstInstancesBuffer = frameData.inFirstInstancesBuffer
            };
            m_indirectCullComputePipeline.record(indirectCullCommandBuffer, frameIdx, m_meshInfoCounter, cullParams);
            indirectCullCommandBuffer.end();
        }

        {
            CommandBuffer& staticMeshCommandBuffer = frameData.staticMeshRenderCommandBuffer;
            vk::CommandBuffer vkStaticMeshCommandBuffer = staticMeshCommandBuffer.begin(false, &inheritanceInfo);

            const glm::ivec2 viewportSize = m_viewportRect.getSize();
            const vk::Viewport viewport{ 
                .x = (float)m_viewportRect.min.x,
                .y = (float)m_viewportRect.max.y,
                .width = (float)viewportSize.x,
                .height = -((float)viewportSize.y),
                .minDepth = 0.0f,
                .maxDepth = 1.0f };
            const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };

            vkStaticMeshCommandBuffer.setViewport(0, { viewport });
            vkStaticMeshCommandBuffer.setScissor(0, { scissor });
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
            m_staticMeshGraphicsPipeline.record(staticMeshCommandBuffer, frameIdx, m_meshInfoCounter, drawParams);
            staticMeshCommandBuffer.end();
        }

        frameData.updated = true;
    }
    {
        CommandBuffer& imguiCommandBuffer = frameData.imguiCommandBuffer;
        vk::CommandBuffer vkImguiCommandBuffer = imguiCommandBuffer.begin(false, &inheritanceInfo);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkImguiCommandBuffer, nullptr);
        imguiCommandBuffer.end();
    }

    const vk::RenderPassBeginInfo renderPassBeginInfo{
        .renderPass = m_renderPass.getRenderPass(),
        .framebuffer = m_framebuffers.getFramebuffer(m_swapChain.getCurrentImageIdx()),
        .renderArea = vk::Rect2D {.offset = vk::Offset2D { 0, 0 }, .extent = extent },
        .clearValueCount = (uint32)s_clearValues.size(),
        .pClearValues = s_clearValues.data(),
    };

    std::array<vk::CommandBuffer, 2> secondaryCommandBuffers = 
        { frameData.staticMeshRenderCommandBuffer.getCommandBuffer(), frameData.imguiCommandBuffer.getCommandBuffer() };

    CommandBuffer& commandBuffer = frameData.primaryCommandBuffer;
    vk::CommandBuffer vkCommandBuffer = commandBuffer.begin(true);
    /*
    {
        vk::MemoryBarrier2 memoryBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eHost,
            .srcAccessMask = vk::AccessFlagBits2::eHostWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &memoryBarrier });
    }*/

    vk::CommandBuffer vkIndirectCullCommandBuffer = frameData.indirectCullCommandBuffer.getCommandBuffer();
    vkCommandBuffer.executeCommands(1, &vkIndirectCullCommandBuffer);
    vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eSecondaryCommandBuffers);
    vkCommandBuffer.executeCommands((uint32)secondaryCommandBuffers.size(), secondaryCommandBuffers.data());
    vkCommandBuffer.endRenderPass();

    commandBuffer.end();
}

void RendererVK::setHaveToRecordCommandBuffers()
{
    for (PerFrameData& perFrame : m_perFrameData)
        perFrame.updated = false;
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