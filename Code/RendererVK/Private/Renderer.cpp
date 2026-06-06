module RendererVK:Renderer;

import Core;
import Core.fwd;
import Core.glm;
import Core.Window;
import Core.Frustum;
import Core.imgui;
import Core.Camera;

import File.FileSystem;
import File.ITextureData;

import :RenderNode;
import :VK;
import :StagingManager;
import :TextureManager;
import :MeshDataManager;
import :glslang;
import :Layout;
import :ObjectContainer;

// Fits one orthographic light-space view-projection per cascade to the camera's view frustum,
// stabilized to the shadow-map texel grid to avoid shimmering as the camera moves. Frustum corners
// are derived analytically from the camera parameters so the result is independent of the projection
// matrix's depth convention. outSplits holds each cascade's far distance from the camera.
namespace
{
    void computeSunCascades(const Camera& camera, float aspect, const glm::vec3& sunDir,
        glm::mat4(&outViewProj)[RendererVKLayout::NUM_SHADOW_CASCADES], glm::vec4& outSplits)
    {
        constexpr uint32 N = RendererVKLayout::NUM_SHADOW_CASCADES;
        const float shadowNear = camera.near;
        const float shadowFar = 500.0f; // sun shadows are capped well short of the camera far plane
        const float res = (float)RendererVKLayout::SHADOW_MAP_RESOLUTION;

        const glm::mat4 invView = glm::inverse(camera.viewMatrix);
        const glm::vec3 camPos = glm::vec3(invView[3]);
        const glm::vec3 right = glm::normalize(glm::vec3(invView[0]));
        const glm::vec3 up = glm::normalize(glm::vec3(invView[1]));
        const glm::vec3 forward = -glm::normalize(glm::vec3(invView[2])); // right-handed: -Z is forward
        const float tanHalfV = tanf(glm::radians(camera.fovDeg) * 0.5f);

        float splits[N + 1];
        splits[0] = shadowNear;
        for (uint32 i = 1; i <= N; ++i)
        {
            const float p = (float)i / (float)N;
            const float logd = shadowNear * powf(shadowFar / shadowNear, p);
            const float lind = shadowNear + (shadowFar - shadowNear) * p;
            splits[i] = glm::mix(lind, logd, 0.7f); // practical split scheme
        }

        const glm::vec3 L = glm::normalize(sunDir);
        const glm::vec3 upRef = (fabsf(L.y) > 0.99f) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);

        for (uint32 c = 0; c < N; ++c)
        {
            const float dists[2] = { splits[c], splits[c + 1] };
            glm::vec3 corners[8];
            int idx = 0;
            for (int di = 0; di < 2; ++di)
            {
                const float d = dists[di];
                const float h = d * tanHalfV;
                const float w = h * aspect;
                const glm::vec3 cc = camPos + forward * d;
                corners[idx++] = cc + up * h + right * w;
                corners[idx++] = cc + up * h - right * w;
                corners[idx++] = cc - up * h + right * w;
                corners[idx++] = cc - up * h - right * w;
            }
            glm::vec3 center(0.0f);
            for (int k = 0; k < 8; ++k) center += corners[k];
            center /= 8.0f;
            float radius = 0.0f;
            for (int k = 0; k < 8; ++k) radius = glm::max(radius, glm::length(corners[k] - center));
            radius = ceilf(radius * 16.0f) / 16.0f;

            const float zPad = 100.0f; // include casters between the light and the cascade volume
            // L points towards the sun, so the light sits up-sun of the scene looking back along -L.
            const glm::vec3 eye = center + L * (radius + zPad);
            const glm::mat4 lightView = glm::lookAtRH(eye, center, upRef);
            glm::mat4 lightProj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + zPad);

            // Snap the projected origin to whole texels to keep the shadow stable under camera motion.
            const glm::mat4 vp = lightProj * lightView;
            glm::vec4 origin = vp * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            origin *= res * 0.5f;
            const glm::vec2 rounded = glm::round(glm::vec2(origin));
            const glm::vec2 off = (rounded - glm::vec2(origin)) * (2.0f / res);
            lightProj[3][0] += off.x;
            lightProj[3][1] += off.y;

            outViewProj[c] = lightProj * lightView;
            outSplits[c] = dists[1];
        }
    }
}

Renderer::~Renderer()
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK::~RendererVK");
    }
    ImGui_ImplVulkan_Shutdown();
}

bool Renderer::initialize(Window& window, EValidation validation, EVSync vsync)
{
    // Disable layers we don't care about for now to eliminate potential issues
    _putenv("DISABLE_LAYER_NV_OPTIMUS_1=True");
    _putenv("DISABLE_VULKAN_OW_OVERLAY_LAYER=True");
    _putenv("DISABLE_VULKAN_OW_OBS_CAPTURE=True");
    _putenv("DISABLE_VULKAN_OBS_CAPTURE=True");

    glslang::InitializeProcess();
    const bool enableValidationLayers = (validation == EValidation::ENABLED);
    if (enableValidationLayers)
    {
        _putenv("VK_LAYER_PRINTF_ENABLE=1");
        _putenv("VK_LAYER_PRINTF_TO_STDOUT=0");
        //_putenv("VK_LAYER_PRINTF_BUFFER_SIZE=16777216");
    }
    m_vsyncEnabled = (vsync == EVSync::ENABLED);
    Globals::instance.initialize(window, enableValidationLayers);
    Globals::instance.setBreakOnValidationLayerError(enableValidationLayers);
    Globals::device.initialize();

    window.getWindowSize(m_windowSize);
    m_surface.initialize(window);
    assert(m_surface.deviceSupportsSurface());

    m_swapChain.initialize(m_surface, RendererVKLayout::NUM_FRAMES_IN_FLIGHT, m_vsyncEnabled);
    m_viewportRect.max = glm::ivec2(m_swapChain.getLayout().extent.width, m_swapChain.getLayout().extent.height);

    Globals::stagingManager.initialize();
    Globals::meshDataManager.initialize(RendererVKLayout::MAX_VERTEX_DATA, RendererVKLayout::MAX_INDEX_DATA);

    m_renderPass.initialize(m_swapChain);
    m_framebuffers.initialize(m_renderPass, m_swapChain);

    initImgui(window);

    m_staticMeshGraphicsPipeline.initialize(m_renderPass);
    m_indirectCullComputePipeline.initialize();
    m_lightGridComputePipeline.initialize();

    for (ShadowMap& shadowMap : m_shadowMaps)
        shadowMap.initialize();
    m_shadowCullComputePipeline.initialize();
    m_shadowMapGraphicsPipeline.initialize(m_shadowMaps[0]); // render pass is compatible across all

    vk::Device vkDevice = Globals::device.getDevice();

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.indirectCullPipelineDescriptorSet.initialize(m_indirectCullComputePipeline.getDescriptorSetLayout());
        perFrame.lightGridPipelineDescriptorSet.initialize(m_lightGridComputePipeline.getDescriptorSetLayout());
        perFrame.staticMeshPipelineDescriptorSet.initialize(m_staticMeshGraphicsPipeline.getDescriptorSetLayout());

        perFrame.shadowCullDescriptorSet.initialize(m_shadowCullComputePipeline.getDescriptorSetLayout());
        perFrame.shadowDrawDescriptorSet.initialize(m_shadowMapGraphicsPipeline.getDescriptorSetLayout());

        perFrame.primaryCommandBuffer.initialize(vk::CommandBufferLevel::ePrimary);
        perFrame.indirectCullCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.lightGridCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.staticMeshRenderCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.imguiCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);

        perFrame.ubo.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

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

        perFrame.lightInfosBuffer.initialize(sizeof(RendererVKLayout::LightInfo) * RendererVKLayout::MAX_LIGHTS,
            vk::BufferUsageFlagBits::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached);
        perFrame.mappedLightInfos = perFrame.lightInfosBuffer.mapMemory<RendererVKLayout::LightInfo>();

        perFrame.lightGridsBuffer.initialize(RendererVKLayout::LIGHT_GRID_BUFFER_SIZE,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal);

        perFrame.lightTableBuffer.initialize(3 * sizeof(uint32) + sizeof(uint32) * RendererVKLayout::LIGHT_TABLE_NUM_ENTRIES,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible);
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

	uint16 diffuseIdx = Globals::textureManager.upload(*ITextureData::createFallbackWhiteTexture(), false);
	assert(diffuseIdx == RendererVKLayout::FALLBACK_DIFFUSE_TEX_IDX);
	uint16 normalIdx = Globals::textureManager.upload(*ITextureData::createFallbackNormalTexture(), false);
	assert(normalIdx == RendererVKLayout::FALLBACK_NORMAL_TEX_IDX);

    return true;
}

void Renderer::recreateWindowSurface(Window& window)
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

void Renderer::recreateSwapchain()
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

void Renderer::reloadShaders()
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK::reloadShaders");
        return;
    }

    m_staticMeshGraphicsPipeline.reloadShaders();
    m_indirectCullComputePipeline.reloadShaders();
    m_lightGridComputePipeline.reloadShaders();
    m_shadowCullComputePipeline.reloadShaders();
    m_shadowMapGraphicsPipeline.reloadShaders();

    setHaveToRecordCommandBuffers();
    printf("Reloaded shaders\n");
}

void Renderer::setWindowMinimized(bool minimized)
{
    m_windowMinimized = minimized;
}

const Frustum& Renderer::beginFrame(const Camera& camera)
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

    // Sun + cascaded shadow maps.
    const float aspect = (float)viewportSize.x / (float)viewportSize.y;
    glm::mat4 cascadeViewProj[RendererVKLayout::NUM_SHADOW_CASCADES];
    glm::vec4 cascadeSplits(0.0f);
    computeSunCascades(camera, aspect, m_sunDirection, cascadeViewProj, cascadeSplits);

    ubo.sunDirection = glm::vec4(m_sunDirection, 0.0f);
    ubo.sunColor = glm::vec4(m_sunColor, 0.0f);
    for (uint32 c = 0; c < RendererVKLayout::NUM_SHADOW_CASCADES; ++c)
        ubo.cascadeViewProj[c] = cascadeViewProj[c];
    ubo.cascadeSplits = cascadeSplits;
    ubo.shadowParams = glm::vec4(0.0015f, 0.25f, 1.0f / (float)RendererVKLayout::SHADOW_MAP_RESOLUTION, 1.0f);

    Globals::stagingManager.upload(frameData.ubo.getBuffer(), sizeof(RendererVKLayout::Ubo), &ubo);

    m_lightCounter = 0;
    return ubo.frustum;
}

void Renderer::renderNodeThreadSafe(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    const uint32 startIdx = std::atomic_ref<uint32>(m_meshInstanceCounter).fetch_add(numInstances);
    assert(startIdx + numInstances <= RendererVKLayout::MAX_INSTANCE_DATA);

    for (auto& pair : node.m_numInstancesPerMesh)
        std::atomic_ref<uint32>(m_numInstancesPerMesh[pair.first]) += pair.second;

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    memcpy(frameData.mappedMeshInstances.data() + startIdx, node.m_meshInstances.data(), numInstances * sizeof(node.m_meshInstances[0]));
}

void Renderer::renderNode(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    const uint32 startIdx = m_meshInstanceCounter;
    m_meshInstanceCounter += numInstances;
    assert(m_meshInstanceCounter <= RendererVKLayout::MAX_INSTANCE_DATA);

    for (auto& pair : node.m_numInstancesPerMesh)
        m_numInstancesPerMesh[pair.first] += pair.second;

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    memcpy(frameData.mappedMeshInstances.data() + startIdx, node.m_meshInstances.data(), numInstances * sizeof(node.m_meshInstances[0]));
}

void Renderer::addLightInfo(const RendererVKLayout::LightInfo& light)
{
    if (m_lightCounter < RendererVKLayout::MAX_LIGHTS)
    {
        PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
        frameData.mappedLightInfos[m_lightCounter] = light;
        m_lightCounter++;
    }
}

void Renderer::addPointLight(const PointLight& light)   { addLightInfo(light); }
void Renderer::addAreaLight(const AreaLight& areaLight) { addLightInfo(areaLight); }
void Renderer::addSpotLight(const SpotLight& spotLight) { addLightInfo(spotLight); }

void Renderer::setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity)
{
    m_sunDirection = glm::normalize(direction);
    m_sunColor = color * intensity;
}

void Renderer::present()
{
    if(m_windowMinimized)
        return;

    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    assert(frameData.mappedRenderNodeTransforms.size() >= m_renderNodeTransforms.size());
    assert(frameData.mappedMeshInstances.size() >= m_meshInstanceCounter);
    assert(frameData.mappedFirstInstances.size() >= m_meshInfoCounter);
    assert(m_renderNodeTransforms.size() == 0 || Globals::textureManager.getNumTextures() > 0 && "Attempting to render object without any textures loaded!");

    memcpy(frameData.mappedRenderNodeTransforms.data(), m_renderNodeTransforms.data(), m_renderNodeTransforms.size() * sizeof(m_renderNodeTransforms[0]));
    uint32 instanceCounter = 0;
    const uint32 numMeshInfos = (uint32)m_numInstancesPerMesh.size();
    for (uint32 meshIdx = 0; meshIdx < numMeshInfos; ++meshIdx)
    {
        frameData.mappedFirstInstances[meshIdx] = instanceCounter;
        instanceCounter += m_numInstancesPerMesh[meshIdx];
    }

    frameData.inRenderNodeTransformsBuffer.flushMappedMemory(m_renderNodeTransforms.size() * sizeof(m_renderNodeTransforms[0]));
    frameData.inMeshInstancesBuffer.flushMappedMemory(m_meshInstanceCounter * sizeof(RendererVKLayout::InMeshInstance));
    frameData.inFirstInstancesBuffer.flushMappedMemory(numMeshInfos * sizeof(uint32));
    frameData.lightInfosBuffer.flushMappedMemory(m_lightCounter * sizeof(RendererVKLayout::LightInfo));

    m_indirectCullComputePipeline.update(frameIdx, m_meshInstanceCounter);
    m_lightGridComputePipeline.update(frameIdx, m_lightCounter);

    vk::Semaphore waitSemaphore = Globals::stagingManager.update();
    if (waitSemaphore != VK_NULL_HANDLE)
    {
		frameData.primaryCommandBuffer.addWaitSemaphore(waitSemaphore, vk::PipelineStageFlagBits::eTransfer);
    }

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

uint32 Renderer::addRenderNodeTransform(const Transform& transform)
{
    const uint32 renderNodeIdx = (uint32)m_renderNodeTransforms.size();
    m_renderNodeTransforms.emplace_back(transform);
    return renderNodeIdx;
}

void Renderer::recordCommandBuffers()
{
    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    vk::CommandBufferInheritanceInfo inheritanceInfo
    {
        .renderPass = m_renderPass.getRenderPass(),
        .subpass = 0,
        .framebuffer = VK_NULL_HANDLE,
        .occlusionQueryEnable = false,
        .queryFlags = {},
        .pipelineStatistics = {}
    };

    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    if (!frameData.updated && m_meshInfoCounter > 0)
    {
        {
            CommandBuffer& indirectCullCommandBuffer = frameData.indirectCullCommandBuffer;
            vk::CommandBufferInheritanceInfo indirectInheritanceInfo;
            indirectCullCommandBuffer.begin(false, &indirectInheritanceInfo);
            IndirectCullComputePipeline::RecordParams cullParams
            {
                .descriptorSet = frameData.indirectCullPipelineDescriptorSet,
                .ubo = frameData.ubo,
                .inRenderNodeTransformsBuffer = frameData.inRenderNodeTransformsBuffer,
                .inMeshInstancesBuffer = frameData.inMeshInstancesBuffer,
                .inMeshInstanceOffsetsBuffer = m_instanceOffsetsBuffer,
                .inMeshInfoBuffer = m_meshInfosBuffer,
                .inFirstInstancesBuffer = frameData.inFirstInstancesBuffer,
            };
            m_indirectCullComputePipeline.record(indirectCullCommandBuffer, frameIdx, m_meshInfoCounter, cullParams);
            indirectCullCommandBuffer.end();
        }

        {
			CommandBuffer& lightGridCommandBuffer = frameData.lightGridCommandBuffer;
			vk::CommandBufferInheritanceInfo lightGridInheritanceInfo;
			lightGridCommandBuffer.begin(false, &lightGridInheritanceInfo);
            LightGridComputePipeline::RecordParams lightGridParams
            {
                .descriptorSet = frameData.lightGridPipelineDescriptorSet,
                .ubo = frameData.ubo,
                .inLightInfoBuffer = frameData.lightInfosBuffer,
                .outLightGridBuffer = frameData.lightGridsBuffer,
                .outLightTableBuffer = frameData.lightTableBuffer,
            };
			m_lightGridComputePipeline.record(frameData.lightGridCommandBuffer, frameIdx, lightGridParams);
			lightGridCommandBuffer.end();
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
                .descriptorSet = frameData.staticMeshPipelineDescriptorSet,
                .ubo = frameData.ubo,
                .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
                .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
                .materialInfoBuffer = m_materialInfosBuffer,
                .instanceIdxBuffer = m_indirectCullComputePipeline.getInstanceIdxBuffer(frameIdx),
                .meshInstanceBuffer = m_indirectCullComputePipeline.getOutMeshInstancesBuffer(frameIdx),
                .indirectCommandBuffer = m_indirectCullComputePipeline.getIndirectCommandBuffer(frameIdx),
                .lightInfosBuffer = frameData.lightInfosBuffer,
                .lightGridsBuffer = frameData.lightGridsBuffer,
                .lightTableBuffer = frameData.lightTableBuffer,
                .shadowMapView = m_shadowMaps[frameIdx].getSampleView(),
                .shadowMapSampler = m_shadowMaps[frameIdx].getSampler(),
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

    constexpr std::array<vk::ClearValue, 2> clearValues 
    {
        vk::ClearColorValue{ std::array<float, 4> { 0.2f, 0.0f, 0.0f, 1.0f }},
        vk::ClearDepthStencilValue{ 1.0f, 0 }
    };
    const vk::RenderPassBeginInfo renderPassBeginInfo{
        .renderPass = m_renderPass.getRenderPass(),
        .framebuffer = m_framebuffers.getFramebuffer(m_swapChain.getCurrentImageIdx()),
        .renderArea = vk::Rect2D {.offset = vk::Offset2D { 0, 0 }, .extent = extent },
        .clearValueCount = (uint32)clearValues.size(),
        .pClearValues = clearValues.data(),
    };

    CommandBuffer& commandBuffer = frameData.primaryCommandBuffer;
    vk::CommandBuffer vkCommandBuffer = commandBuffer.begin(true);

    { // Sync for ubo copy
        vk::MemoryBarrier2 memoryBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eCopy,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eUniformRead,
        };
        vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &memoryBarrier });
    }

    vk::CommandBuffer vkIndirectCullCommandBuffer = frameData.indirectCullCommandBuffer.getCommandBuffer();
	vk::CommandBuffer vkLightGridCommandBuffer = frameData.lightGridCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkStaticMeshRenderCommandBuffer = frameData.staticMeshRenderCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkImguiCommandBuffer = frameData.imguiCommandBuffer.getCommandBuffer();

    if (frameData.updated)
    {
        vkCommandBuffer.executeCommands(1, &vkIndirectCullCommandBuffer);
		vkCommandBuffer.executeCommands(1, &vkLightGridCommandBuffer);
    }

    // Sun shadow cascades: cull casters against each cascade frustum, then render depth-only into
    // that cascade's shadow-map layer. Recorded directly into the primary so the depth is ready
    // before the lighting pass samples it. The camera cull above has already populated the shared
    // transformed-instance buffer that the shadow vertex shader reads.
    if (frameData.updated && m_meshInfoCounter > 0)
    {
        // Build the (cascade-independent) caster list once.
        ShadowCullComputePipeline::RecordParams cullParams{
            .descriptorSet = frameData.shadowCullDescriptorSet,
            .ubo = frameData.ubo,
            .dispatchIndirectBuffer = m_indirectCullComputePipeline.getDispatchIndirectBuffer(frameIdx),
            .inRenderNodeTransformsBuffer = frameData.inRenderNodeTransformsBuffer,
            .inMeshInstancesBuffer = frameData.inMeshInstancesBuffer,
            .inMeshInstanceOffsetsBuffer = m_instanceOffsetsBuffer,
            .inMeshInfoBuffer = m_meshInfosBuffer,
            .inFirstInstancesBuffer = frameData.inFirstInstancesBuffer,
        };
        m_shadowCullComputePipeline.record(frameData.primaryCommandBuffer, frameIdx, m_meshInfoCounter, cullParams);

        // All cascades render in a single multiview render pass; gl_ViewIndex selects the layer.
        ShadowMap& shadowMap = m_shadowMaps[frameIdx];
        const float shadowRes = (float)shadowMap.getResolution();
        const vk::Extent2D shadowExtent{ shadowMap.getResolution(), shadowMap.getResolution() };
        const vk::Viewport shadowViewport{ .x = 0.0f, .y = 0.0f, .width = shadowRes, .height = shadowRes, .minDepth = 0.0f, .maxDepth = 1.0f };
        const vk::Rect2D shadowScissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = shadowExtent };
        vk::ClearValue shadowClear;
        shadowClear.depthStencil = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 };

        const vk::RenderPassBeginInfo shadowRpBegin{
            .renderPass = shadowMap.getRenderPass(),
            .framebuffer = shadowMap.getFramebuffer(),
            .renderArea = vk::Rect2D{ .offset = vk::Offset2D{ 0, 0 }, .extent = shadowExtent },
            .clearValueCount = 1,
            .pClearValues = &shadowClear,
        };
        vkCommandBuffer.beginRenderPass(shadowRpBegin, vk::SubpassContents::eInline);
        vkCommandBuffer.setViewport(0, { shadowViewport });
        vkCommandBuffer.setScissor(0, { shadowScissor });
        ShadowMapGraphicsPipeline::RecordParams shadowDrawParams{
            .descriptorSet = frameData.shadowDrawDescriptorSet,
            .ubo = frameData.ubo,
            .meshInstanceBuffer = m_shadowCullComputePipeline.getOutMeshInstancesBuffer(frameIdx),
            .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
            .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
            .instanceIdxBuffer = m_shadowCullComputePipeline.getInstanceIdxBuffer(frameIdx),
            .indirectCommandBuffer = m_shadowCullComputePipeline.getIndirectCommandBuffer(frameIdx),
        };
        m_shadowMapGraphicsPipeline.record(frameData.primaryCommandBuffer, frameIdx, m_meshInfoCounter, shadowDrawParams);
        vkCommandBuffer.endRenderPass();
    }

    vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eSecondaryCommandBuffers);
    if (frameData.updated)
        vkCommandBuffer.executeCommands(1, &vkStaticMeshRenderCommandBuffer);
    vkCommandBuffer.executeCommands(1, &vkImguiCommandBuffer);
    vkCommandBuffer.endRenderPass();

    commandBuffer.end();
}

void Renderer::setHaveToRecordCommandBuffers()
{
    for (PerFrameData& perFrame : m_perFrameData)
        perFrame.updated = false;
}

void Renderer::addObjectContainer(ObjectContainer* pObjectContainer)
{
    m_objectContainers.push_back(pObjectContainer);
}

uint32 Renderer::addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos)
{
    const uint32 baseMeshInfoIdx = m_meshInfoCounter;
    m_meshInfoCounter += (uint32)meshInfos.size();
    m_numInstancesPerMesh.resize(m_meshInfoCounter);

    assert(m_meshInfoCounter < USHRT_MAX);
    assert(m_meshInfoCounter < RendererVKLayout::MAX_UNIQUE_MESHES);

    Globals::stagingManager.upload(m_meshInfosBuffer.getBuffer(), meshInfos.size() * sizeof(RendererVKLayout::MeshInfo),
        meshInfos.data(), baseMeshInfoIdx * sizeof(RendererVKLayout::MeshInfo));

    setHaveToRecordCommandBuffers();
    return baseMeshInfoIdx;
}

uint32 Renderer::addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos)
{
    const uint32 baseMaterialInfoIdx = m_materialInfoCounter;
    m_materialInfoCounter += (uint32)materialInfos.size();
    assert(m_materialInfoCounter < USHRT_MAX);
    assert(m_materialInfoCounter < RendererVKLayout::MAX_UNIQUE_MATERIALS);

    Globals::stagingManager.upload(m_materialInfosBuffer.getBuffer(), materialInfos.size() * sizeof(RendererVKLayout::MaterialInfo),
        materialInfos.data(), baseMaterialInfoIdx * sizeof(RendererVKLayout::MaterialInfo));

    setHaveToRecordCommandBuffers();
    return baseMaterialInfoIdx;
}

uint32 Renderer::addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets)
{
    const uint32 baseInstanceOffsetIdx = m_instanceOffsetCounter;
    m_instanceOffsetCounter += (uint32)meshInstanceOffsets.size();
    assert(m_instanceOffsetCounter < RendererVKLayout::MAX_INSTANCE_OFFSETS);

    Globals::stagingManager.upload(m_instanceOffsetsBuffer.getBuffer(), meshInstanceOffsets.size() * sizeof(RendererVKLayout::MeshInstanceOffset),
        meshInstanceOffsets.data(), baseInstanceOffsetIdx * sizeof(RendererVKLayout::MeshInstanceOffset));

    setHaveToRecordCommandBuffers();
    return baseInstanceOffsetIdx;
}

void Renderer::initImgui(Window& window)
{
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
    init_info.DescriptorPool = nullptr;
    init_info.DescriptorPoolSize = 10;
    init_info.MinImageCount = 2;
    init_info.ImageCount = 2;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = imgui_check_vk_result;
    ImGui_ImplVulkan_Init(&init_info);

    ImGui_ImplVulkan_PipelineInfo pipeline_create;
    pipeline_create.RenderPass = m_renderPass.getRenderPass();
    pipeline_create.Subpass = 0;
    pipeline_create.MSAASamples = {};
    ImGui_ImplVulkan_CreateMainPipeline(&pipeline_create);
}

Renderer::Stats Renderer::getStats()
{
    Stats stats;

    stats.numLights = m_lightCounter;
    stats.maxLights = RendererVKLayout::MAX_LIGHTS;

    stats.numMeshInstances = m_meshInstanceCounter;
    stats.maxMeshInstances = RendererVKLayout::MAX_INSTANCE_DATA;

    stats.numInstanceOffsets = m_instanceOffsetCounter;
    stats.maxInstanceOffsets = RendererVKLayout::MAX_INSTANCE_OFFSETS;

    stats.numMeshTypes = m_meshInfoCounter;
    stats.maxMeshTypes = RendererVKLayout::MAX_UNIQUE_MESHES;

    stats.numMaterials = m_materialInfoCounter;
    stats.maxMaterials = RendererVKLayout::MAX_UNIQUE_MATERIALS;

    stats.numRenderNodes = (uint32)m_renderNodeTransforms.size();
    stats.maxRenderNodes = RendererVKLayout::MAX_RENDER_NODES;

    stats.numTextures = (uint32)Globals::textureManager.getNumTextures();
	stats.maxTextures = RendererVKLayout::MAX_TEXTURES;

	stats.vertexDataUsedBytes = Globals::meshDataManager.getVertexBufUsed();
	stats.maxVertexDataBytes = Globals::meshDataManager.getVertexBufSize();

	stats.indexDataUsedBytes = Globals::meshDataManager.getIndexBufUsed();
	stats.maxIndexDataBytes = Globals::meshDataManager.getIndexBufSize();

    stats.numObjectContainers = (uint32)m_objectContainers.size();

    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();

    PerFrameData& lastFrameData = m_perFrameData[(frameIdx - 1) % m_perFrameData.size()];
    struct LightGridInfo
    {
        uint32 inout_numGrids;
        uint32 inout_gridDataCounter;
    } info;
    std::span<LightGridInfo> infoSpan = lastFrameData.lightTableBuffer.mapMemory<LightGridInfo>(0, sizeof(LightGridInfo));
    //lastFrameData.lightTableBuffer.flushMappedMemory(sizeof(LightGridInfo));
    info = *infoSpan.data();
    lastFrameData.lightTableBuffer.unmapMemory();
    stats.numLightGrids = info.inout_numGrids;
    stats.maxLightGrids = RendererVKLayout::LIGHT_TABLE_NUM_ENTRIES / 2; // We don't want to go over 50% because of hash table collisions
    stats.lightGridMemUsageBytes = info.inout_gridDataCounter * sizeof(uint32);
    stats.maxLightGridMemUsageBytes = RendererVKLayout::LIGHT_GRID_BUFFER_SIZE;

    return stats;
}