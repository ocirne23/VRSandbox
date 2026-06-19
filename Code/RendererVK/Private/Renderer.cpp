module RendererVK.Renderer;

import Core;
import Core.fwd;
import Core.glm;
import Core.Window;
import Core.Frustum;
import Core.imgui;
import Core.Camera;
import Core.Tweaks;

import File.FileSystem;
import File.ITextureData;

import RendererVK.RenderNode;
import RendererVK.VK;
import RendererVK.Allocator;
import RendererVK.StagingManager;
import RendererVK.TextureManager;
import RendererVK.MeshDataManager;
import RendererVK.glslang;
import RendererVK.Layout;
import RendererVK.ObjectContainer;

// Fits one orthographic light-space view-projection per cascade to the camera's view frustum,
// stabilized to the shadow-map texel grid to avoid shimmering as the camera moves. Frustum corners
// are derived analytically from the camera parameters so the result is independent of the projection
// matrix's depth convention. outSplits holds each cascade's far distance from the camera.
namespace
{
    // Radical inverse in an arbitrary base; (Halton(2), Halton(3)) gives the low-discrepancy sub-pixel jitter
    // sequence used by the TAA accumulation.
    float radicalInverse(uint32 i, uint32 base)
    {
        float invBase = 1.0f / (float)base;
        float result = 0.0f;
        float f = invBase;
        while (i > 0)
        {
            result += (float)(i % base) * f;
            i /= base;
            f *= invBase;
        }
        return result;
    }

    // Solar eclipse: fraction of the sun's disc (plus its glow halo, which acts as an extended light
    // source) the moon leaves visible. 1 = no overlap, 0 = totality, with the annular 1 - rm^2/rs^2
    // floor when the moon is smaller than sun + corona. Radii and separation are chord lengths
    // (sqrt(2-2cos) == the angle for discs this small); the partial phase uses a smoothstep area
    // approximation (within a few percent of the exact circle-circle lens area).
    float sunVisibleFraction(const glm::vec3& sunDir, const glm::vec3& moonDir,
        float cosSunRadius, float cosMoonRadius, float sunGlow)
    {
        const float rs = sqrtf(std::max(2.0f - 2.0f * cosSunRadius, 0.0f)) + sunGlow * 0.02f;
        const float rm = sqrtf(std::max(2.0f - 2.0f * cosMoonRadius, 0.0f));
        const float d = glm::distance(sunDir, moonDir);
        const float contained = rm >= rs ? 0.0f : 1.0f - (rm * rm) / (rs * rs);
        return glm::mix(contained, 1.0f, glm::smoothstep(fabsf(rs - rm), rs + rm, d));
    }

    void computeSunCascades(const Camera& camera, float aspect, const glm::vec3& sunDir,
        glm::mat4(&outViewProj)[RendererVKLayout::NUM_SHADOW_CASCADES])
    {
        constexpr uint32 N = RendererVKLayout::NUM_SHADOW_CASCADES;
        const float shadowNear = camera.near;
        // --- Cascade distribution knobs --------------------------------------------------------
        // shadowFar:    max distance from the camera that receives sun shadows. Lower = every cascade
        //               covers less ground = higher resolution everywhere (at the cost of range).
        // splitLambda:  0 = evenly spaced splits (uniform), 1 = logarithmic (splits bunch up close to
        //               the camera). Higher = more shadow-map resolution near the camera.
        const float shadowFar = 250.0f;
        const float splitLambda = 0.80f;
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
            splits[i] = glm::mix(lind, logd, splitLambda); // practical split scheme
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

            // How far up-sun a caster can be above this cascade and still be captured. Extends only the
            // near side of the depth slab (the far plane stays tight to the bounding sphere), so the only
            // cost is depth precision over a longer range (fine for D32). It must exceed the tallest
            // caster's height above the cascade; too small and casters get clipped out of the depth map,
            // making their shadows pop/disappear as the cascade slab slides with camera motion.
            const float zPad = 250.0f;
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

            glm::mat4 viewProj = lightProj * lightView;
            // Stash per-cascade scalars in the matrix's structurally-zero bottom row: far distance in
            // m[0][3], world texel size (ortho width / resolution) in m[1][3], ortho depth range (world
            // units, converts normalized depth gaps back to world for the PCSS penumbra) in m[2][3].
            // Shaders mask these back to the canonical [0,0,0,1] bottom row before using the matrix.
            viewProj[0][3] = dists[1];
            viewProj[1][3] = (2.0f * radius) / res;
            viewProj[2][3] = 2.0f * radius + zPad;
            outViewProj[c] = viewProj;
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
    destroyEyeCompositeTargets();
    ImGui_ImplVulkan_Shutdown();
}

bool Renderer::initialize(Window& window, EValidation validation, EVSync vsync, EVr vr)
{
    // Disable layers we don't care about for now to eliminate potential issues
    _putenv("DISABLE_LAYER_NV_OPTIMUS_1=True");
    _putenv("DISABLE_VULKAN_OW_OVERLAY_LAYER=True");
    _putenv("DISABLE_VULKAN_OW_OBS_CAPTURE=True");
    _putenv("DISABLE_VULKAN_OBS_CAPTURE=True");

    auto rerecordCallback = [this]() { setHaveToRecordCommandBuffers(); };
    m_skyParams.registerTweaks();
    m_fogParams.registerTweaks();
    m_rtParams.registerTweaks();
    m_rtaoParams.registerTweaks(rerecordCallback, [this]() { if (Globals::device.getGraphicsQueue().waitIdle() != vk::Result::eSuccess) return; m_rtaoPipeline.reloadShaders(); setHaveToRecordCommandBuffers(); });
    m_taaParams.registerTweaks(rerecordCallback);
    m_postParams.registerTweaks(rerecordCallback);

    glslang::InitializeProcess();
    const bool enableValidationLayers = (validation == EValidation::ENABLED);
    if (enableValidationLayers)
    {
        _putenv("VK_LAYER_PRINTF_ENABLE=1");
        _putenv("VK_LAYER_PRINTF_TO_STDOUT=0");
        //_putenv("VK_LAYER_PRINTF_BUFFER_SIZE=16777216");
    }
    m_vsyncEnabled = (vsync == EVSync::ENABLED);

    if (vr == EVr::ENABLED)
    {
        m_taaParams.taaFeedback *= 0.5f; // Reduce blur for VR
        Globals::openXR.initInstanceAndSystem();
    }

    Globals::instance.initialize(window, enableValidationLayers);
    Globals::instance.setBreakOnValidationLayerError(enableValidationLayers);
    Globals::device.initialize();

    if (Globals::openXR.isEnabled())
    {
        if (!Globals::openXR.createSession(Globals::instance.getInstance(), Globals::device.getPhysicalDevice(),
            Globals::device.getDevice(), Globals::device.getGraphicsQueueIndex(), 0))
        {
            printf("Renderer: failed to create OpenXR session, continuing without VR.\n");
            Globals::openXR.destroy(); // clears isEnabled()
        }
    }

    window.getWindowSize(m_windowSize);
    m_surface.initialize(window);
    assert(m_surface.deviceSupportsSurface());

    m_swapChain.initialize(m_surface, RendererVKLayout::NUM_FRAMES_IN_FLIGHT, m_vsyncEnabled);
    m_viewportRect.max = glm::ivec2(m_swapChain.getLayout().extent.width, m_swapChain.getLayout().extent.height);

    Globals::stagingManager.initialize();
    Globals::meshDataManager.initialize(RendererVKLayout::INITIAL_VERTEX_DATA, RendererVKLayout::INITIAL_INDEX_DATA);

    m_renderPass.initialize(m_swapChain);
    m_framebuffers.initialize(m_renderPass, m_swapChain);

    initImgui(window);

    const vk::Extent2D ext = m_swapChain.getLayout().extent;

    m_sceneViewCount = Globals::openXR.isEnabled() ? 2u : 1u;

    for (PerFrameData& perFrame : m_perFrameData)
        perFrame.sceneColor.initialize(RendererVKLayout::SCENE_COLOR_FORMAT, ext.width, ext.height, m_sceneViewCount);
    const vk::RenderPass sceneRenderPass = m_perFrameData[0].sceneColor.getRenderPass();

    m_maxTextures = Globals::textureManager.getDescriptorCap(); // fixed layout cap; live count grows separately
    m_staticMeshGraphicsPipeline.initialize(sceneRenderPass, m_maxUniqueMeshes, m_maxTextures, m_sceneViewCount > 1);
    m_rtaoPipeline.initialize(&m_rtaoParams, ext.width, ext.height, m_maxTextures, m_numTextureDescriptors, m_sceneViewCount);
    m_volumetricFogPipeline.initialize();
    m_volumetricFogPipeline.initializeApply(sceneRenderPass, m_sceneViewCount);
    m_taaPipeline.initialize(ext.width, ext.height, m_sceneViewCount);
    m_eyeAdaptationPipeline.initialize();
    m_compositePipeline.initialize(m_renderPass);
    m_indirectCullComputePipeline.initialize(m_maxInstanceData, m_maxUniqueMeshes);
    m_lightGridComputePipeline.initialize();
    m_accelStructure.initialize(m_maxUniqueMeshes);
    m_giProbePipeline.initialize(m_maxGiTlasInstances, m_maxTextures, m_numTextureDescriptors);
    m_giProbePipeline.initializeDebug(sceneRenderPass);


    m_shadowCullComputePipeline.initialize(m_maxInstanceData, m_maxUniqueMeshes);
    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.gbuffer.initialize(ext.width, ext.height, m_sceneViewCount);
        perFrame.shadowMap.initialize();
    }
    m_shadowMapGraphicsPipeline.initialize(m_perFrameData[0].shadowMap, m_maxUniqueMeshes, m_maxTextures);
    m_gbufferPipeline.initialize(m_perFrameData[0].gbuffer, m_maxTextures);

    vk::Device vkDevice = Globals::device.getDevice();

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.indirectCullPipelineDescriptorSet.initialize(m_indirectCullComputePipeline.getDescriptorSetLayout());
        perFrame.lightGridPipelineDescriptorSet.initialize(m_lightGridComputePipeline.getDescriptorSetLayout());
        for (uint32 eye = 0; eye < m_sceneViewCount; ++eye)
        {
            perFrame.staticMeshPipelineDescriptorSet[eye].initialize(m_staticMeshGraphicsPipeline.getDescriptorSetLayout(), m_numTextureDescriptors);
            perFrame.gbufferDescriptorSet[eye].initialize(m_gbufferPipeline.getDescriptorSetLayout(), m_numTextureDescriptors);
        }
        perFrame.compositeDescriptorSet.initialize(m_compositePipeline.getDescriptorSetLayout());

        perFrame.shadowCullDescriptorSet.initialize(m_shadowCullComputePipeline.getDescriptorSetLayout());
        perFrame.shadowDrawDescriptorSet.initialize(m_shadowMapGraphicsPipeline.getDescriptorSetLayout(), m_numTextureDescriptors);

        perFrame.primaryCommandBuffer.initialize(vk::CommandBufferLevel::ePrimary);
        perFrame.staticMeshCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.gbufferCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.aoCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.indirectCullCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.lightGridCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.imguiCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.shadowCullCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.shadowDrawCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.globalIllumCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.volumetricFogCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.fogApplyCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.giProbeDebugCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.taaCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.eyeAdaptCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);
        perFrame.compositeCommandBuffer.initialize(vk::CommandBufferLevel::eSecondary);

        perFrame.ubo.initialize(sizeof(RendererVKLayout::Ubo),
            vk::BufferUsageFlagBits2::eUniformBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal, false, "Ubo");

        perFrame.inRenderNodeTransformsBuffer.initialize(m_maxRenderNodes * sizeof(RendererVKLayout::RenderNodeTransform),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "RenderNodeTransforms", BufferHostAccess::eSequentialWrite);
        perFrame.mappedRenderNodeTransforms = perFrame.inRenderNodeTransformsBuffer.mapMemory<RendererVKLayout::RenderNodeTransform>();

        // Kept cached/random: growMeshInstanceCapacity reads the existing mapping to preserve in-flight
        // instances across a resize, so this buffer must stay CPU-readable.
        perFrame.inMeshInstancesBuffer.initialize(m_maxInstanceData * sizeof(RendererVKLayout::InMeshInstance),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached, false, "MeshInstances");
        perFrame.mappedMeshInstances = perFrame.inMeshInstancesBuffer.mapMemory<RendererVKLayout::InMeshInstance>();

        perFrame.inFirstInstancesBuffer.initialize(m_maxUniqueMeshes * sizeof(uint32),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "FirstInstances", BufferHostAccess::eSequentialWrite);
        perFrame.mappedFirstInstances = perFrame.inFirstInstancesBuffer.mapMemory<uint32>();

        perFrame.lightInfosBuffer.initialize(sizeof(RendererVKLayout::LightInfo) * RendererVKLayout::MAX_LIGHTS,
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "LightInfos", BufferHostAccess::eSequentialWrite);
        perFrame.mappedLightInfos = perFrame.lightInfosBuffer.mapMemory<RendererVKLayout::LightInfo>();

        perFrame.fogVolumesBuffer.initialize(sizeof(RendererVKLayout::FogVolumes),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "FogVolumes", BufferHostAccess::eSequentialWrite);
        perFrame.mappedFogVolumes = perFrame.fogVolumesBuffer.mapMemory<RendererVKLayout::FogVolumes>();
        perFrame.mappedFogVolumes.data()->count = 0;

    }
    createLightGridBuffers();

    if (m_sceneViewCount > 1)
    {
        for (uint32 i = 0; i < 2; ++i)
            m_vrCompositeDescriptorSet[i].initialize(m_compositePipeline.getDescriptorSetLayout());
        createEyeCompositeTargets();
    }

    m_meshInfosBuffer.initialize(m_maxUniqueMeshes * sizeof(RendererVKLayout::MeshInfo),
        vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal, true, "MeshInfos");

    m_materialInfosBuffer.initialize(m_maxUniqueMaterials * sizeof(RendererVKLayout::MaterialInfo),
        vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal, true, "MaterialInfos");

    m_instanceOffsetsBuffer.initialize(m_maxInstanceOffsets * sizeof(RendererVKLayout::MeshInstanceOffset),
        vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
        vk::MemoryPropertyFlagBits::eDeviceLocal, true, "InstanceOffsets");

	uint16 diffuseIdx = Globals::textureManager.upload(*ITextureData::createFallbackWhiteTexture(), false);
	assert(diffuseIdx == RendererVKLayout::FALLBACK_DIFFUSE_TEX_IDX);
	uint16 normalIdx = Globals::textureManager.upload(*ITextureData::createFallbackNormalTexture(), false);
	assert(normalIdx == RendererVKLayout::FALLBACK_NORMAL_TEX_IDX);

    m_gpuCrashTracker.Initialize(false);

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

    const vk::Extent2D ext = m_swapChain.getLayout().extent;
    m_rtaoPipeline.recreateImages(ext.width, ext.height);
    m_taaPipeline.recreateImages(ext.width, ext.height);

    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.gbuffer.initialize(ext.width, ext.height, m_sceneViewCount);
        perFrame.sceneColor.initialize(RendererVKLayout::SCENE_COLOR_FORMAT, ext.width, ext.height, m_sceneViewCount);
    }
    createEyeCompositeTargets(); // VR: resize the per-eye LDR composite targets

    // The cached scene command buffers embed the (now-recreated) scene-colour render pass in their
    // inheritance info, so force them to re-record against the new handle.
    setHaveToRecordCommandBuffers();
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
    const vk::Extent2D ext = m_swapChain.getLayout().extent;
    m_rtaoPipeline.recreateImages(ext.width, ext.height);
    m_taaPipeline.recreateImages(ext.width, ext.height);
    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.gbuffer.initialize(ext.width, ext.height, m_sceneViewCount);
        perFrame.sceneColor.initialize(RendererVKLayout::SCENE_COLOR_FORMAT, ext.width, ext.height, m_sceneViewCount);
    }
    createEyeCompositeTargets(); // VR: resize the per-eye LDR composite targets

    // Cached scene command buffers reference the recreated scene-colour render pass; re-record them.
    setHaveToRecordCommandBuffers();
}

void Renderer::reloadShaders()
{
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    if (waitResult != vk::Result::eSuccess)
    {
        assert(false && "Failed to wait for device idle in RendererVK::reloadShaders");
        return;
    }

    m_staticMeshGraphicsPipeline.reloadShaders(m_perFrameData[0].sceneColor.getRenderPass(), m_maxTextures);
    m_gbufferPipeline.reloadShaders(m_perFrameData[m_swapChain.getPrevFrameIdx()].gbuffer);
    m_rtaoPipeline.reloadShaders();
    m_volumetricFogPipeline.reloadShaders(m_perFrameData[0].sceneColor.getRenderPass());
    m_indirectCullComputePipeline.reloadShaders();
    m_lightGridComputePipeline.reloadShaders();
    m_shadowCullComputePipeline.reloadShaders();
    m_shadowMapGraphicsPipeline.reloadShaders(m_maxTextures);
    m_giProbePipeline.reloadShaders(m_maxTextures);
    m_giProbePipeline.reloadDebugShaders(m_perFrameData[0].sceneColor.getRenderPass());
    m_taaPipeline.reloadShaders();
    m_eyeAdaptationPipeline.reloadShaders();
    m_compositePipeline.reloadShaders(m_renderPass);

    setHaveToRecordCommandBuffers();
    printf("Reloaded shaders\n");
}

void Renderer::setWindowMinimized(bool minimized)
{
    m_windowMinimized = minimized;
}

const Frustum& Renderer::beginFrame(const Camera& cameraIn)
{
    Globals::openXR.pollEvents();

    Camera camera = cameraIn;
    const glm::quat vrBaseOrientation = glm::quat_cast(glm::inverse(cameraIn.viewMatrix)) * cameraIn.playSpaceOrientation;
    if (Globals::openXR.beginFrame())
    {
        glm::mat4 headView;
        glm::vec3 headPos;
        Globals::openXR.getHeadView(cameraIn.position, vrBaseOrientation, headView, headPos);
        camera.viewMatrix = headView;
        camera.position = headPos;
    }

    // Mesh instances overflowed mid-frame last frame
    if (m_pendingMaxInstanceData > m_maxInstanceData)
        growMeshInstanceCapacity(m_pendingMaxInstanceData);
    // Last frame's instance count (still live here) outgrew the GI TLAS instance buffers.
    if (m_meshInstanceCounter > m_maxGiTlasInstances)
    {
        while (m_maxGiTlasInstances < m_meshInstanceCounter)
            m_maxGiTlasInstances *= 2;
        waitForGpuAndFlushStaging();
        m_giProbePipeline.resizeTlasInstanceBuffers(m_maxGiTlasInstances);
        printf("Renderer: grew GI TLAS instance capacity to %u\n", m_maxGiTlasInstances);
    }
    // A mesh mega-buffer was reallocated (vertex/index data growth)
    if (Globals::meshDataManager.getGeneration() != m_meshDataGeneration)
    {
        m_meshDataGeneration = Globals::meshDataManager.getGeneration();
        setHaveToRecordCommandBuffers();
    }

    // The live texture count outgrew the variable-count texture-array descriptors
    if (Globals::textureManager.getGeneration() != m_textureGeneration)
    {
        m_textureGeneration = Globals::textureManager.getGeneration();
        m_numTextureDescriptors = Globals::textureManager.getMaxTextures();
        waitForGpuAndFlushStaging();
        m_giProbePipeline.resizeTextureDescriptors(m_numTextureDescriptors);
        m_rtaoPipeline.resizeTextureDescriptors(m_numTextureDescriptors);
        for (PerFrameData& perFrame : m_perFrameData)
        {
            for (uint32 eye = 0; eye < m_sceneViewCount; ++eye)
            {
                perFrame.staticMeshPipelineDescriptorSet[eye].initialize(m_staticMeshGraphicsPipeline.getDescriptorSetLayout(), m_numTextureDescriptors);
                perFrame.gbufferDescriptorSet[eye].initialize(m_gbufferPipeline.getDescriptorSetLayout(), m_numTextureDescriptors);
            }
            perFrame.shadowDrawDescriptorSet.initialize(m_shadowMapGraphicsPipeline.getDescriptorSetLayout(), m_numTextureDescriptors);
        }
        setHaveToRecordCommandBuffers();
    }

    checkLightGridCapacity();

    m_meshInstanceCounter = 0;
    memset(m_numInstancesPerMesh.data(), 0, m_numInstancesPerMesh.size() * sizeof(m_numInstancesPerMesh[0]));

    const glm::ivec2 viewportSize = m_viewportRect.getSize();
    // In VR the "centre view" (used for culling, GI region, shadow cascade fit, and the shared screen-space
    // froxel fog volume) uses a head-centred projection spanning the union of both eyes' FOV, so it covers
    // everything either eye renders. Desktop uses the plain camera perspective.
    const glm::mat4x4 projection = Globals::openXR.isEnabled()
        ? Globals::openXR.getCombinedProjection(camera.near, camera.far)
        : glm::perspective(glm::radians(camera.fovDeg), (float)viewportSize.x / (float)viewportSize.y, camera.near, camera.far);
    glm::mat4 viewMatrix = camera.viewMatrix;

    glm::vec2 taaJitterNdc(0.0f);
    if (m_taaParams.taaEnabled && viewportSize.x > 0 && viewportSize.y > 0)
    {
        const uint32 sampleIdx = (m_frameCounter % 16u) + 1u;
        taaJitterNdc.x = (radicalInverse(sampleIdx, 2u) - 0.5f) * 2.0f / (float)viewportSize.x;
        taaJitterNdc.y = (radicalInverse(sampleIdx, 3u) - 0.5f) * 2.0f / (float)viewportSize.y;
    }

    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    m_cameraPos = camera.position; // drives the GI probe region each frame

    static RendererVKLayout::Ubo ubo;

    const uint32 numViews = Globals::openXR.isEnabled() ? RendererVKLayout::NUM_UBO_VIEWS : 1;
    for (uint32 v = 0; v < numViews; ++v)
    {
        ubo.views[v].prevMvp = ubo.views[v].mvp;
        ubo.views[v].prevInvMvp = ubo.views[v].invMvp;
    }

    RendererVKLayout::ViewData& centerView = ubo.views[RendererVKLayout::VIEW_CENTER];
    centerView.mvp = projection * viewMatrix;
    centerView.invMvp = glm::inverse(centerView.mvp);
    centerView.viewPos = glm::vec4(camera.position, 1.0f);
    ubo.frustum.fromMatrix(centerView.mvp);

    if (Globals::openXR.isEnabled())
    {
        for (uint32 eye = 0; eye < 2; ++eye)
        {
            glm::mat4 eyeView;
            glm::vec3 eyePos;
            Globals::openXR.getEyeView(eye, cameraIn.position, vrBaseOrientation, eyeView, eyePos);
            const glm::mat4 eyeProj = Globals::openXR.getEyeProjection(eye, camera.near, camera.far);
            RendererVKLayout::ViewData& v = ubo.views[eye + 1]; // [1] = left eye, [2] = right eye
            v.mvp = eyeProj * eyeView;
            v.invMvp = glm::inverse(v.mvp);
            v.viewPos = glm::vec4(eyePos, 1.0f);
        }
    }

    const vk::Extent2D swapExtent = m_swapChain.getLayout().extent;
    ubo.screenSize = glm::vec4((float)swapExtent.width, (float)swapExtent.height,
        1.0f / (float)swapExtent.width, 1.0f / (float)swapExtent.height);
    ubo.viewportRect = glm::vec4(
        (float)m_viewportRect.min.x / (float)swapExtent.width,
        (float)m_viewportRect.min.y / (float)swapExtent.height,
        (float)viewportSize.x / (float)swapExtent.width,
        (float)viewportSize.y / (float)swapExtent.height);
    ubo.taaJitter = glm::vec4(taaJitterNdc, 0.0f, 0.0f);
    ubo.aoParams = glm::vec4(m_rtaoParams.enabled ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
    ubo.frameIndex = m_frameCounter;

    const SkyParams& sky = m_skyParams;
    const FogParams& fog = m_fogParams;

    // Earth sea-level scattering coefficients, scaled by the atmosphere tweaks.
    ubo.betaRayleigh = glm::vec3(5.802e-6f, 13.558e-6f, 33.1e-6f) * sky.rayleighScatter;
    ubo.rolloffKnee = sky.sunRolloffKnee;
    ubo.betaMie = 3.996e-6f * sky.mieScatter;
    ubo.sunDirection = sky.sunDirection;
    ubo.sunAngularCos = sky.sunAngularCos;
    // Solar eclipse: fold the moon-covered sun fraction into the UBO sun color so every sun consumer
    // (sky atmosphere, forward lighting, GI trace, volumetric fog, clouds) dims consistently.
    const float eclipseVisible = sunVisibleFraction(sky.sunDirection, glm::normalize(sky.moonDirection), sky.sunAngularCos, cosf(glm::radians(sky.moonSizeDeg)), sky.sunGlow);
    ubo.sunColor = sky.sunColor * sky.sunIntensity;
    ubo.eclipseParams = glm::vec4(eclipseVisible, sky.sunRolloffHeadroom, 0.0f, 0.0f);
    ubo.sunGlow = sky.sunGlow;

    ubo.skyRadianceColor = sky.skyRadianceColor * sky.skyRadianceIntensity;
    ubo.rtSkyRadiance = m_rtParams.rtSkyRadiance ? 1.0f : 0.0f;
    ubo.ambientColor = sky.ambientColor * sky.ambientIntensity;
    ubo.skyUp = sky.up;
    ubo.rtSunShadow = m_rtParams.rtSunShadow ? 1.0f : 0.0f;

    if (!m_rtParams.rtSunShadow)
    {
        const float aspect = (float)viewportSize.x / (float)viewportSize.y;
        glm::mat4 cascadeViewProj[RendererVKLayout::NUM_SHADOW_CASCADES];
        computeSunCascades(camera, aspect, sky.sunDirection, cascadeViewProj);
        for (uint32 c = 0; c < RendererVKLayout::NUM_SHADOW_CASCADES; ++c)
            ubo.cascadeViewProj[c] = cascadeViewProj[c];
        ubo.shadowParams = glm::vec3(sky.shadowDepthBias, sky.shadowNormalBias, 1.0f / (float)RendererVKLayout::SHADOW_MAP_RESOLUTION);
    }

    ubo.sunShadowRays = (float)m_rtParams.sunShadowRays;
    ubo.rtLightShadows = m_rtParams.rtLightShadows ? 1.0f : 0.0f;
    static const Clock::time_point timeStart = Clock::now();
    ubo.timeSeconds = std::chrono::duration<float>(Clock::now() - timeStart).count();
    ubo.cloudCoverage = sky.cloudCoverage;
    ubo.cloudThickness = sky.cloudThickness * sky.cloudThickness;
    ubo.cloudParams0 = glm::vec4(sky.cloudHeight, 0.00012f * sky.cloudScale, 0.0043f * sky.cloudWindSpeed, sky.cloudWindAngle);
    ubo.cloudParams1 = glm::vec4(sky.cloudSoftness, sky.cloudShading, 0.0f, 0.0f);
    ubo.cloudParams2 = glm::vec4(sky.cloudDensity, sky.cloudSharpness, sky.cloudBaseVar, sky.moonBrightness);
    ubo.skySunParams = glm::vec4(sky.scatterBoost, sky.mieG, sky.sunRolloff, sky.starDensity);

    ubo.fogParams0 = glm::vec4(fog.density, fog.heightBase, fog.heightFalloff * fog.heightFalloff, fog.range);
    ubo.fogParams1 = glm::vec4(fog.albedo * fog.albedoIntensity, fog.anisotropy);
    ubo.fogParams2 = glm::vec4(fog.noiseScale, fog.noiseStrength, fog.windSpeed, fog.temporalBlend);
    ubo.fogParams3 = glm::vec4(0.0f, 0.0f, fog.enabled ? 1.0f : 0.0f, fog.lightShadows ? 1.0f : 0.0f);
    ubo.fogParams4 = glm::vec4((float)fog.sunRays, fog.spatialFilter ? 1.0f : 0.0f, fog.giAmbient ? 1.0f : 0.0f, fog.sunSoftness);

    ubo.moonParams = glm::vec4(glm::normalize(sky.moonDirection), cosf(glm::radians(sky.moonSizeDeg)));
    ubo.starParams = glm::vec4(sky.starSize, sky.starSizeVar, sky.starBrightness, sky.starColorVar);
    ubo.nebulaParams = glm::vec4(sky.nebulaIntensity, sky.nebulaScale, sky.nebulaBandWidth, sky.nebulaDust);
    ubo.nebulaAxis = glm::vec4(glm::normalize(sky.nebulaAxis), 0.0f);
    ubo.atmosParams = glm::vec4(sky.rayleighHeight, sky.mieHeight, sky.mieExtinction, sky.ozone);
    ubo.groundParams = glm::vec4(sky.groundColor * sky.groundIntensity, 0.0f);

    Globals::stagingManager.upload(frameData.ubo.getBuffer(), sizeof(RendererVKLayout::Ubo), &ubo);

    m_lightCounter = 0;
    m_fogVolumeCounter = 0;
    m_frameCounter++;
    return ubo.frustum;
}

void Renderer::renderNodeThreadSafe(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    const uint32 startIdx = std::atomic_ref<uint32>(m_meshInstanceCounter).fetch_add(numInstances);
    if (startIdx + numInstances > m_maxInstanceData)
    {
        // Doesn't fit this frame: return the space, drop the node, and grow at the next beginFrame.
        std::atomic_ref<uint32>(m_meshInstanceCounter).fetch_sub(numInstances);
        std::atomic_ref<uint32> pending(m_pendingMaxInstanceData);
        uint32 cur = pending.load(std::memory_order_relaxed);
        while (cur < startIdx + numInstances && !pending.compare_exchange_weak(cur, startIdx + numInstances)) {}
        return;
    }

    for (auto& pair : node.m_numInstancesPerMesh)
        std::atomic_ref<uint32>(m_numInstancesPerMesh[pair.first]) += pair.second;

    PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    memcpy(frameData.mappedMeshInstances.data() + startIdx, node.m_meshInstances.data(), numInstances * sizeof(node.m_meshInstances[0]));
}

void Renderer::renderNode(const RenderNode& node)
{
    const uint32 numInstances = (uint32)node.m_meshInstances.size();
    const uint32 startIdx = m_meshInstanceCounter;
    if (startIdx + numInstances > m_maxInstanceData)
    {
        growMeshInstanceCapacity(startIdx + numInstances);
    }
    m_meshInstanceCounter += numInstances;

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

void Renderer::addFogVolume(const RendererVKLayout::FogVolumeInfo& fogVolume)
{
    if (m_fogVolumeCounter < RendererVKLayout::MAX_FOG_VOLUMES)
    {
        PerFrameData& frameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
        frameData.mappedFogVolumes.data()->volumes[m_fogVolumeCounter] = fogVolume;
        m_fogVolumeCounter++;
    }
}

void Renderer::addPointLight(const PointLight& light)   { addLightInfo(light); }
void Renderer::addAreaLight(const AreaLight& areaLight) { addLightInfo(areaLight); }
void Renderer::addSpotLight(const SpotLight& spotLight) { addLightInfo(spotLight); }

void Renderer::setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity)
{
    m_skyParams.sunDirection = glm::normalize(direction);
    m_skyParams.sunColor = color;
    m_skyParams.sunIntensity = intensity;
}

void Renderer::present()
{
    if(m_windowMinimized)
    {
        Globals::openXR.endFrame(nullptr, nullptr, {}, vk::ImageLayout::eUndefined); // balance the begun XR frame
        return;
    }

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

    frameData.mappedFogVolumes.data()->count = m_fogVolumeCounter;
    frameData.fogVolumesBuffer.flushMappedMemory(RendererVKLayout::FOG_VOLUME_HEADER_SIZE + m_fogVolumeCounter * sizeof(RendererVKLayout::FogVolumeInfo));

    m_indirectCullComputePipeline.update(frameIdx, m_meshInstanceCounter);
    m_lightGridComputePipeline.update(frameIdx, m_lightCounter);

    vk::Semaphore waitSemaphore = Globals::stagingManager.update();
    if (waitSemaphore != VK_NULL_HANDLE)
		frameData.primaryCommandBuffer.addWaitSemaphore(waitSemaphore, vk::PipelineStageFlagBits::eAllCommands); // eAllCommands (not just transfer) for GI BLAS

    if (!m_swapChain.acquireNextImage())
    {
        Globals::openXR.endFrame(nullptr, nullptr, {}, vk::ImageLayout::eUndefined); // balance the begun XR frame
        recreateSwapchain();
        return;
    }
    recordCommandBuffers();

    m_swapChain.submitCommandBuffer(getCurrentCommandBuffer());

    Globals::openXR.endFrame(m_eyeColorImage[0], m_eyeColorImage[1], m_swapChain.getLayout().extent, vk::ImageLayout::ePresentSrcKHR);

    if (!m_swapChain.present())
    {
        recreateSwapchain();
    }
}

uint32 Renderer::addRenderNodeTransform(const Transform& transform)
{
    const uint32 renderNodeIdx = (uint32)m_renderNodeTransforms.size();
    m_renderNodeTransforms.emplace_back(transform);
    if ((uint32)m_renderNodeTransforms.size() > m_maxRenderNodes)
        growRenderNodeCapacity((uint32)m_renderNodeTransforms.size());
    return renderNodeIdx;
}

namespace
{
    uint32 growCapacity(uint32 current, uint32 needed, uint32 limit = UINT32_MAX)
    {
        uint64 capacity = current;
        while (capacity < needed)
            capacity *= 2;
        assert(needed <= limit);
        return (uint32)std::min<uint64>(capacity, limit);
    }
}

void Renderer::waitForGpuAndFlushStaging()
{
    // Queued staging copies may target a buffer about to be destroyed; submit them first, then drain
    // the GPU so every buffer being recreated is no longer in use.
    Globals::stagingManager.flushPending();
    auto waitResult = Globals::device.getGraphicsQueue().waitIdle();
    assert(waitResult == vk::Result::eSuccess && "Failed to wait for device idle during capacity growth");
}

void Renderer::growRenderNodeCapacity(uint32 needed)
{
    m_maxRenderNodes = growCapacity(m_maxRenderNodes, needed);
    waitForGpuAndFlushStaging();
    for (PerFrameData& perFrame : m_perFrameData)
    {
        // No contents to preserve: present() re-copies the full CPU transform list every frame.
        perFrame.inRenderNodeTransformsBuffer.initialize(m_maxRenderNodes * sizeof(RendererVKLayout::RenderNodeTransform),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "RenderNodeTransforms", BufferHostAccess::eSequentialWrite);
        perFrame.mappedRenderNodeTransforms = perFrame.inRenderNodeTransformsBuffer.mapMemory<RendererVKLayout::RenderNodeTransform>();
    }
    setHaveToRecordCommandBuffers();
    printf("Renderer: grew render node capacity to %u\n", m_maxRenderNodes);
}

void Renderer::growMeshInstanceCapacity(uint32 needed)
{
    m_maxInstanceData = growCapacity(m_maxInstanceData, needed);
    waitForGpuAndFlushStaging();

    // renderNode() grows inline mid-frame, so the instances already written to the current frame's
    // mapped buffer this frame must survive the reallocation. (At the beginFrame call site the counter
    // holds last frame's count and the copy is redundant but harmless.)
    PerFrameData& currentFrameData = m_perFrameData[m_swapChain.getCurrentFrameIndex()];
    std::vector<RendererVKLayout::InMeshInstance> writtenInstances(currentFrameData.mappedMeshInstances.begin(), currentFrameData.mappedMeshInstances.begin() + m_meshInstanceCounter);

    for (PerFrameData& perFrame : m_perFrameData)
    {
        // Kept cached/random: growMeshInstanceCapacity reads the existing mapping to preserve in-flight
        // instances across a resize, so this buffer must stay CPU-readable.
        perFrame.inMeshInstancesBuffer.initialize(m_maxInstanceData * sizeof(RendererVKLayout::InMeshInstance),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCached, false, "MeshInstances");
        perFrame.mappedMeshInstances = perFrame.inMeshInstancesBuffer.mapMemory<RendererVKLayout::InMeshInstance>();
    }
    memcpy(currentFrameData.mappedMeshInstances.data(), writtenInstances.data(), writtenInstances.size() * sizeof(RendererVKLayout::InMeshInstance));

    m_indirectCullComputePipeline.resizeInstanceBuffers(m_maxInstanceData);
    m_shadowCullComputePipeline.resizeInstanceBuffers(m_maxInstanceData);
    setHaveToRecordCommandBuffers();
    printf("Renderer: grew mesh instance capacity to %u\n", m_maxInstanceData);
}

void Renderer::growUniqueMeshCapacity(uint32 needed)
{
    m_maxUniqueMeshes = growCapacity(m_maxUniqueMeshes, needed, RendererVKLayout::MESH_MATERIAL_INDEX_LIMIT);
    waitForGpuAndFlushStaging();
    for (PerFrameData& perFrame : m_perFrameData)
    {
        // No contents to preserve: first-instance offsets are rewritten every present().
        perFrame.inFirstInstancesBuffer.initialize(m_maxUniqueMeshes * sizeof(uint32),
            vk::BufferUsageFlagBits2::eStorageBuffer,
            vk::MemoryPropertyFlagBits::eHostVisible, false, "FirstInstances", BufferHostAccess::eSequentialWrite);
        perFrame.mappedFirstInstances = perFrame.inFirstInstancesBuffer.mapMemory<uint32>();
    }
    m_meshInfosBuffer.resize(m_maxUniqueMeshes * sizeof(RendererVKLayout::MeshInfo));
    m_accelStructure.resizeBlasAddressBuffer(m_maxUniqueMeshes);
    m_indirectCullComputePipeline.resizeCommandBuffers(m_maxUniqueMeshes);
    m_shadowCullComputePipeline.resizeCommandBuffers(m_maxUniqueMeshes);
    m_staticMeshGraphicsPipeline.resizeMeshCapacity(m_maxUniqueMeshes);
    m_shadowMapGraphicsPipeline.resizeMeshCapacity(m_maxUniqueMeshes);
    setHaveToRecordCommandBuffers();
    printf("Renderer: grew unique mesh capacity to %u\n", m_maxUniqueMeshes);
}

void Renderer::growMaterialCapacity(uint32 needed)
{
    m_maxUniqueMaterials = growCapacity(m_maxUniqueMaterials, needed, RendererVKLayout::MESH_MATERIAL_INDEX_LIMIT);
    waitForGpuAndFlushStaging();
    m_materialInfosBuffer.resize(m_maxUniqueMaterials * sizeof(RendererVKLayout::MaterialInfo));
    setHaveToRecordCommandBuffers();
    printf("Renderer: grew material capacity to %u\n", m_maxUniqueMaterials);
}

void Renderer::createLightGridBuffers()
{
    for (PerFrameData& perFrame : m_perFrameData)
    {
        perFrame.lightGridsBuffer.initialize(m_lightGridBufferSize,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal, false, "LightGrids");

        // Device-local + host-visible (ReBAR) so GPU writes are fast; the CPU reads the header in getStats,
        // hence the default random (cached-where-possible) access.
        perFrame.lightTableBuffer.initialize(3 * sizeof(uint32) + sizeof(uint32) * m_lightTableEntries,
            vk::BufferUsageFlagBits2::eStorageBuffer | vk::BufferUsageFlagBits2::eTransferDst,
            vk::MemoryPropertyFlagBits::eDeviceLocal | vk::MemoryPropertyFlagBits::eHostVisible, false, "LightTable");

        // Zero the readback header so the capacity check / stats never see uninitialized counters
        // before the first recorded frame fills them in.
        std::span<uint32> header = perFrame.lightTableBuffer.mapMemory<uint32>(0, 3 * sizeof(uint32));
        memset(header.data(), 0, 3 * sizeof(uint32));
        perFrame.lightTableBuffer.flushMappedMemory(3 * sizeof(uint32));
        perFrame.lightTableBuffer.unmapMemory();
    }
}

void Renderer::growLightGridBuffers(size_t neededGridBytes, uint32 neededTableEntries)
{
    while (m_lightGridBufferSize < neededGridBytes)
        m_lightGridBufferSize *= 2;
    while (m_lightTableEntries < neededTableEntries)
        m_lightTableEntries *= 2; // stays a power of 2 for the hash
    waitForGpuAndFlushStaging();
    createLightGridBuffers(); // per-frame GPU scratch, rebuilt every frame: nothing to preserve
    setHaveToRecordCommandBuffers();
    printf("Renderer: grew light grid buffers to %zu bytes / %u table entries\n", m_lightGridBufferSize, m_lightTableEntries);
}

void Renderer::checkLightGridCapacity()
{
    // Read last frame's usage counters (same readback as getStats) and grow before the buffers fill:
    // above a quarter of the table (hash collision quality) or 3/4 of the grid data. Growth targets FIT
    // the observed demand (with headroom) rather than doubling once: a burst can overshoot the current
    // capacity many times over within a single frame. When the shader runs out of space it drops lights
    // for that frame (getOrInsertGrid -> INVALID_GRID) but keeps incrementing gridDataCounter, so the
    // readback measures the true demand of an overflowed frame.
    PerFrameData& lastFrameData = m_perFrameData[(m_swapChain.getCurrentFrameIndex() + m_perFrameData.size() - 1) % m_perFrameData.size()];
    struct LightGridInfo { uint32 numGrids; uint32 gridDataCounter; };
    std::span<LightGridInfo> infoSpan = lastFrameData.lightTableBuffer.mapMemory<LightGridInfo>(0, sizeof(LightGridInfo));
    const LightGridInfo info = *infoSpan.data();
    lastFrameData.lightTableBuffer.unmapMemory();

    const bool tableHigh = info.numGrids > m_lightTableEntries / 4;
    const bool gridHigh = (size_t)info.gridDataCounter * sizeof(uint32) > m_lightGridBufferSize / 4 * 3;
    if (tableHigh || gridHigh)
        growLightGridBuffers(gridHigh ? (size_t)(info.gridDataCounter * sizeof(uint32) * 1.5f) : m_lightGridBufferSize, tableHigh ? info.numGrids * 8 : m_lightTableEntries);
}

void Renderer::growInstanceOffsetCapacity(uint32 needed)
{
    m_maxInstanceOffsets = growCapacity(m_maxInstanceOffsets, needed);
    waitForGpuAndFlushStaging();
    m_instanceOffsetsBuffer.resize(m_maxInstanceOffsets * sizeof(RendererVKLayout::MeshInstanceOffset));
    setHaveToRecordCommandBuffers();
    printf("Renderer: grew instance offset capacity to %u\n", m_maxInstanceOffsets);
}

void Renderer::recordIndirectCull(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    CommandBuffer& cb = frameData.indirectCullCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);
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
    m_indirectCullComputePipeline.record(cb, frameIdx, m_meshInfoCounter, cullParams);
    cb.end();
}

void Renderer::recordLightGrid(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    CommandBuffer& cb = frameData.lightGridCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);
    LightGridComputePipeline::RecordParams params
    {
        .descriptorSet = frameData.lightGridPipelineDescriptorSet,
        .ubo = frameData.ubo,
        .inLightInfoBuffer = frameData.lightInfosBuffer,
        .outLightGridBuffer = frameData.lightGridsBuffer,
        .outLightTableBuffer = frameData.lightTableBuffer,
        .numTableEntries = m_lightTableEntries,
    };
    m_lightGridComputePipeline.record(cb, frameIdx, params);
    cb.end();
}

void Renderer::recordShadowCull(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    CommandBuffer& cb = frameData.shadowCullCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);

    ShadowCullComputePipeline::RecordParams params{
        .descriptorSet = frameData.shadowCullDescriptorSet,
        .ubo = frameData.ubo,
        .dispatchIndirectBuffer = m_indirectCullComputePipeline.getDispatchIndirectBuffer(frameIdx),
        .inRenderNodeTransformsBuffer = frameData.inRenderNodeTransformsBuffer,
        .inMeshInstancesBuffer = frameData.inMeshInstancesBuffer,
        .inMeshInstanceOffsetsBuffer = m_instanceOffsetsBuffer,
        .inMeshInfoBuffer = m_meshInfosBuffer,
        .inFirstInstancesBuffer = frameData.inFirstInstancesBuffer,
        .inMaterialInfoBuffer = m_materialInfosBuffer,
    };
    m_shadowCullComputePipeline.record(cb, frameIdx, m_meshInfoCounter, params);
    cb.end();
}

void Renderer::recordShadowDraw(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    ShadowMap& shadowMap = frameData.shadowMap;
    vk::CommandBufferInheritanceInfo inheritance{ .renderPass = shadowMap.getRenderPass() };
    CommandBuffer& cb = frameData.shadowDrawCommandBuffer;
    vk::CommandBuffer vkCb = cb.begin(false, &inheritance);

    // All cascades render in a single multiview render pass; gl_ViewIndex selects the layer.
    const vk::Extent2D shadowExtent{ shadowMap.getResolution(), shadowMap.getResolution() };
    const float shadowRes = (float)shadowMap.getResolution();
    const vk::Viewport viewport{ .x = 0.0f, .y = 0.0f, .width = shadowRes, .height = shadowRes, .minDepth = 0.0f, .maxDepth = 1.0f };
    const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = shadowExtent };
    vkCb.setViewport(0, { viewport });
    vkCb.setScissor(0, { scissor });

    ShadowMapGraphicsPipeline::RecordParams params{
        .descriptorSet = frameData.shadowDrawDescriptorSet,
        .ubo = frameData.ubo,
        .meshInstanceBuffer = m_shadowCullComputePipeline.getOutMeshInstancesBuffer(frameIdx),
        .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
        .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
        .instanceIdxBuffer = m_shadowCullComputePipeline.getInstanceIdxBuffer(frameIdx),
        .indirectCommandBuffer = m_shadowCullComputePipeline.getIndirectCommandBuffer(frameIdx),
    };
    m_shadowMapGraphicsPipeline.record(cb, frameIdx, m_meshInfoCounter, params);
    cb.end();
}

void Renderer::recordStaticMesh(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBufferInheritanceInfo inheritance{ .renderPass = frameData.sceneColor.getRenderPass() };
    CommandBuffer& cb = frameData.staticMeshCommandBuffer;
    cb.begin(false, &inheritance);
    recordStaticMeshInto(cb, frameIdx, 0);
    cb.end();
}

void Renderer::recordStaticMeshInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBuffer vkCb = cb.getCommandBuffer();

    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::ivec2 viewportSize = m_viewportRect.getSize();
    const vk::Viewport viewport{
        .x = (float)m_viewportRect.min.x,
        .y = (float)m_viewportRect.max.y,
        .width = (float)viewportSize.x,
        .height = -((float)viewportSize.y),
        .minDepth = 0.0f,
        .maxDepth = 1.0f };
    const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };
    vkCb.setViewport(0, { viewport });
    vkCb.setScissor(0, { scissor });
    StaticMeshGraphicsPipeline::RecordParams drawParams
    {
        .descriptorSet = frameData.staticMeshPipelineDescriptorSet[eyeIndex],
        .ubo = frameData.ubo,
        .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
        .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
        .materialInfoBuffer = m_materialInfosBuffer,
        .instanceIdxBuffer = m_indirectCullComputePipeline.getInstanceIdxBuffer(frameIdx),
        .meshInstanceBuffer = m_indirectCullComputePipeline.getOutMeshInstancesBuffer(frameIdx),
        .indirectCommandBuffer = m_indirectCullComputePipeline.getIndirectCommandBuffer(frameIdx),
        .transparentIndirectCommandBuffer = m_indirectCullComputePipeline.getTransparentIndirectCommandBuffer(frameIdx),
        .lightInfosBuffer = frameData.lightInfosBuffer,
        .lightGridsBuffer = frameData.lightGridsBuffer,
        .lightTableBuffer = frameData.lightTableBuffer,
        .giGridDataBuffer = m_giProbePipeline.getGiGridDataBuffer(),
        .meshInfoBuffer = m_meshInfosBuffer,
        .rtMeshInstancesBuffer = frameData.inMeshInstancesBuffer,
        .shadowMapView = frameData.shadowMap.getSampleView(),
        .shadowMapSampler = frameData.shadowMap.getSampler(),
        .shadowMapDepthSampler = frameData.shadowMap.getDepthSampler(),
        .gbufferDepthView = frameData.gbuffer.getDepthView(eyeIndex),
        .gbufferSampler = frameData.gbuffer.getSampler(),
        .viewIndex = RendererVKLayout::eyeToViewIndex(eyeIndex, m_sceneViewCount),
    };
    // Each eye has its own descriptor set (per-eye AO + gbuffer depth), so both eyes write their own.
    m_staticMeshGraphicsPipeline.record(cb, frameIdx, m_meshInfoCounter, drawParams, true);
}

void Renderer::recordGBufferInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBuffer vkCb = cb.getCommandBuffer();

    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::ivec2 viewportSize = m_viewportRect.getSize();
    const vk::Viewport viewport{
        .x = (float)m_viewportRect.min.x,
        .y = (float)m_viewportRect.max.y,
        .width = (float)viewportSize.x,
        .height = -((float)viewportSize.y),
        .minDepth = 0.0f,
        .maxDepth = 1.0f };
    const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };
    vkCb.setViewport(0, { viewport });
    vkCb.setScissor(0, { scissor });
    GBufferPipeline::RecordParams gbufferParams{
        .descriptorSet = frameData.gbufferDescriptorSet[eyeIndex],
        .ubo = frameData.ubo,
        .meshInstanceBuffer = m_indirectCullComputePipeline.getOutMeshInstancesBuffer(frameIdx),
        .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
        .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
        .instanceIdxBuffer = m_indirectCullComputePipeline.getInstanceIdxBuffer(frameIdx),
        .indirectCommandBuffer = m_indirectCullComputePipeline.getIndirectCommandBuffer(frameIdx),
        .materialInfoBuffer = m_materialInfosBuffer,
    };
    m_gbufferPipeline.record(cb, frameIdx, m_meshInfoCounter, gbufferParams, RendererVKLayout::eyeToViewIndex(eyeIndex, m_sceneViewCount));
}

void Renderer::recordGBuffer(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBufferInheritanceInfo inheritance{ .renderPass = frameData.gbuffer.getRenderPass() };
    CommandBuffer& cb = frameData.gbufferCommandBuffer;
    cb.begin(false, &inheritance);
    recordGBufferInto(cb, frameIdx, 0);
    cb.end();
}

// AO trace+denoise for one eye (compute, no render pass); reads that eye's G-buffer, writes that eye's AO.
void Renderer::recordAOInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    GBuffer& gbuffer = frameData.gbuffer;
    const vk::AccelerationStructureKHR tlas = m_accelStructure.getTlas(frameIdx);
    if (m_meshInfoCounter == 0 || !tlas)
        return;
    const uint32 prevFrameIdx = (frameIdx + 1) % RendererVKLayout::NUM_FRAMES_IN_FLIGHT;
    RTAOPipeline::RecordParams aoParams{
        .ubo = frameData.ubo,
        .gbufferNormalView = gbuffer.getNormalView(eyeIndex),
        .gbufferDepthView = gbuffer.getDepthView(eyeIndex),
        .prevGbufferDepthView = m_perFrameData[prevFrameIdx].gbuffer.getDepthView(eyeIndex),
        .gbufferSampler = gbuffer.getSampler(),
        .tlas = tlas,
        .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
        .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
        .meshInfos = m_meshInfosBuffer,
        .meshInstances = frameData.inMeshInstancesBuffer,
        .materialInfos = m_materialInfosBuffer,
    };
    m_rtaoPipeline.record(cb, frameIdx, eyeIndex, aoParams);
}

// Fog apply draw for one eye, inside the eye's scene-colour render pass (viewport set by the caller).
void Renderer::recordFogApplyInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBuffer vkCb = cb.getCommandBuffer();
    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::ivec2 vpMin = m_viewportRect.min;
    const glm::ivec2 vpSize = m_viewportRect.getSize();
    vkCb.setViewport(0, vk::Viewport{ .x = 0.0f, .y = 0.0f, .width = (float)extent.width, .height = (float)extent.height, .minDepth = 0.0f, .maxDepth = 1.0f });
    vkCb.setScissor(0, vk::Rect2D{ .offset = vk::Offset2D{ vpMin.x, vpMin.y }, .extent = vk::Extent2D{ (uint32)vpSize.x, (uint32)vpSize.y } });
    VolumetricFogPipeline::ApplyParams params{
        .ubo = frameData.ubo,
        .gbufferDepthView = frameData.gbuffer.getDepthView(eyeIndex),
        .gbufferSampler = frameData.gbuffer.getSampler(),
    };
    m_volumetricFogPipeline.recordApply(cb, frameIdx, eyeIndex, params);
}

// TAA resolve for one eye (compute, no render pass); reads that eye's scene colour + depth, writes its history.
void Renderer::recordTaaInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    GBuffer& gbuffer = frameData.gbuffer;
    SceneColor& sceneColor = frameData.sceneColor;
    const uint32 prevFrameIdx = (frameIdx + 1) % RendererVKLayout::NUM_FRAMES_IN_FLIGHT;
    TaaPipeline::RecordParams taaParams{
        .ubo = frameData.ubo,
        .currentColorView = sceneColor.getColorLayerView(eyeIndex),
        .currentColorSampler = sceneColor.getSampler(),
        .gbufferDepthView = gbuffer.getDepthView(eyeIndex),
        .prevGbufferDepthView = m_perFrameData[prevFrameIdx].gbuffer.getDepthView(eyeIndex),
        .gbufferSampler = gbuffer.getSampler(),
        .feedback = m_taaParams.taaEnabled ? m_taaParams.taaFeedback : 0.0f,
    };
    m_taaPipeline.record(cb, frameIdx, eyeIndex, taaParams);
}

void Renderer::recordGiProbeDebug(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBufferInheritanceInfo inheritance{ .renderPass = frameData.sceneColor.getRenderPass() };
    CommandBuffer& cb = frameData.giProbeDebugCommandBuffer;
    vk::CommandBuffer vkCb = cb.begin(false, &inheritance);
    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::ivec2 vpSize = m_viewportRect.getSize();
    const vk::Viewport viewport{ .x = (float)m_viewportRect.min.x, .y = (float)m_viewportRect.max.y,
        .width = (float)vpSize.x, .height = -((float)vpSize.y), .minDepth = 0.0f, .maxDepth = 1.0f };
    const vk::Rect2D scissor{ .offset = vk::Offset2D{ 0, 0 }, .extent = extent };
    vkCb.setViewport(0, { viewport });
    vkCb.setScissor(0, { scissor });
    m_giProbePipeline.recordDebugDraw(cb, frameIdx, frameData.ubo, m_giProbeDebugRadius, m_giProbeDebugMode);
    cb.end();
}

void Renderer::recordAO(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    GBuffer& gbuffer = frameData.gbuffer;
    CommandBuffer& cb = frameData.aoCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);
    const vk::AccelerationStructureKHR tlas = m_accelStructure.getTlas(frameIdx);
    if (m_rtaoParams.enabled && m_meshInfoCounter > 0 && tlas)
    {
        const uint32 prevFrameIdx = (frameIdx + 1) % RendererVKLayout::NUM_FRAMES_IN_FLIGHT;
        RTAOPipeline::RecordParams aoParams{
            .ubo = frameData.ubo,
            .gbufferNormalView = gbuffer.getNormalView(),
            .gbufferDepthView = gbuffer.getDepthView(),
            .prevGbufferDepthView = m_perFrameData[prevFrameIdx].gbuffer.getDepthView(),
            .gbufferSampler = gbuffer.getSampler(),
            .tlas = tlas,
            .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
            .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
            .meshInfos = m_meshInfosBuffer,
            .meshInstances = frameData.inMeshInstancesBuffer,
            .materialInfos = m_materialInfosBuffer,
        };
        m_rtaoPipeline.record(cb, frameIdx, 0, aoParams);
    }
    cb.end();
}

void Renderer::recordVolumetricFog(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    CommandBuffer& cb = frameData.volumetricFogCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);
    const vk::AccelerationStructureKHR tlas = m_accelStructure.getTlas(frameIdx);
    if (m_meshInfoCounter > 0 && tlas)
    {
        VolumetricFogPipeline::RecordParams params{
            .ubo = frameData.ubo,
            .lightInfosBuffer = frameData.lightInfosBuffer,
            .lightGridsBuffer = frameData.lightGridsBuffer,
            .lightTableBuffer = frameData.lightTableBuffer,
            .fogVolumesBuffer = frameData.fogVolumesBuffer,
            .giGridDataBuffer = m_giProbePipeline.getGiGridDataBuffer(),
            .shadowMapView = frameData.shadowMap.getSampleView(),
            .shadowMapSampler = frameData.shadowMap.getSampler(),
            .tlas = tlas,
        };
        m_volumetricFogPipeline.record(cb, frameIdx, params);
    }
    cb.end();
}

void Renderer::recordFogApply(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBufferInheritanceInfo inheritance{ .renderPass = frameData.sceneColor.getRenderPass() };
    CommandBuffer& cb = frameData.fogApplyCommandBuffer;
    vk::CommandBuffer vkCb = cb.begin(false, &inheritance);

    // Fullscreen triangle in full-render-target UV space (like the composite), scissored to the viewport.
    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::ivec2 vpMin = m_viewportRect.min;
    const glm::ivec2 vpSize = m_viewportRect.getSize();
    vkCb.setViewport(0, vk::Viewport{ .x = 0.0f, .y = 0.0f, .width = (float)extent.width, .height = (float)extent.height, .minDepth = 0.0f, .maxDepth = 1.0f });
    vkCb.setScissor(0, vk::Rect2D{ .offset = vk::Offset2D{ vpMin.x, vpMin.y }, .extent = vk::Extent2D{ (uint32)vpSize.x, (uint32)vpSize.y } });

    VolumetricFogPipeline::ApplyParams params{
        .ubo = frameData.ubo,
        .gbufferDepthView = frameData.gbuffer.getDepthView(),
        .gbufferSampler = frameData.gbuffer.getSampler(),
    };
    m_volumetricFogPipeline.recordApply(cb, frameIdx, 0, params);
    cb.end();
}

void Renderer::recordTaa(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    GBuffer& gbuffer = frameData.gbuffer;
    SceneColor& sceneColor = frameData.sceneColor;
    CommandBuffer& cb = frameData.taaCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);
    const uint32 prevFrameIdx = (frameIdx + 1) % RendererVKLayout::NUM_FRAMES_IN_FLIGHT;
    TaaPipeline::RecordParams taaParams{
        .ubo = frameData.ubo,
        .currentColorView = sceneColor.getColorView(),
        .currentColorSampler = sceneColor.getSampler(),
        .gbufferDepthView = gbuffer.getDepthView(),
        .prevGbufferDepthView = m_perFrameData[prevFrameIdx].gbuffer.getDepthView(),
        .gbufferSampler = gbuffer.getSampler(),
        .feedback = m_taaParams.taaEnabled ? m_taaParams.taaFeedback : 0.0f,
    };
    m_taaPipeline.record(cb, frameIdx, 0, taaParams);
    cb.end();
}

void Renderer::recordEyeAdaptation(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    CommandBuffer& cb = frameData.eyeAdaptCommandBuffer;
    vk::CommandBufferInheritanceInfo inheritance;
    cb.begin(false, &inheritance);
    EyeAdaptationPipeline::RecordParams params{
        .resolvedView = m_taaPipeline.getResolvedView(frameIdx, 0),
        .sampler = m_taaPipeline.getSampler(),
        .viewportMin = m_viewportRect.min,
        .viewportSize = m_viewportRect.getSize(),
    };
    m_eyeAdaptationPipeline.record(cb, frameIdx, params);
    cb.end();
}

void Renderer::recordComposite(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    vk::CommandBufferInheritanceInfo inheritance { .renderPass = m_renderPass.getRenderPass(), };
    CommandBuffer& cb = frameData.compositeCommandBuffer;
    vk::CommandBuffer vkCb = cb.begin(false, &inheritance);

    const vk::Extent2D extent = m_swapChain.getLayout().extent;
    const glm::ivec2 vpMin = m_viewportRect.min;
    const glm::ivec2 vpSize = m_viewportRect.getSize();
    vkCb.setViewport(0, vk::Viewport{.x = 0.0f, .y = 0.0f, .width = (float)extent.width, .height = (float)extent.height, .minDepth = 0.0f, .maxDepth = 1.0f });
    vkCb.setScissor(0, vk::Rect2D{.offset = vk::Offset2D{ vpMin.x, vpMin.y }, .extent = vk::Extent2D{ (uint32)vpSize.x, (uint32)vpSize.y } });

    CompositePipeline::RecordParams params{
        .descriptorSet = frameData.compositeDescriptorSet,
        .resolvedView = m_taaPipeline.getResolvedView(frameIdx, 0),
        .sampler = m_taaPipeline.getSampler(),
        .exposureBuffer = m_eyeAdaptationPipeline.getExposureBuffer().getBuffer(),
        .exposureEV = m_postParams.exposureEV,
        .tonemapper = m_postParams.tonemapper,
        .autoExposure = m_postParams.autoExposure ? 1 : 0,
    };
    m_compositePipeline.record(cb, params);
    cb.end();
}

void Renderer::createEyeCompositeTargets()
{
    if (m_sceneViewCount <= 1)
        return;
    destroyEyeCompositeTargets();
    vk::Device vkDevice = Globals::device.getDevice();
    const vk::Extent2D ext = m_swapChain.getLayout().extent;
    const vk::Format colorFormat = m_swapChain.getLayout().surfaceFormat.format;

    // Shared depth (the swapchain render pass declares a depth attachment; the composite doesn't use it).
    vk::ImageCreateInfo depthInfo{
        .imageType = vk::ImageType::e2D, .format = vk::Format::eD32Sfloat, .extent = { ext.width, ext.height, 1 },
        .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eDepthStencilAttachment, .sharingMode = vk::SharingMode::eExclusive, .initialLayout = vk::ImageLayout::eUndefined };
    Globals::gpuAllocator.createImage(depthInfo, m_eyeDepthImage, m_eyeDepthMem, "VR.eyeDepth");
    vk::ImageViewCreateInfo depthViewInfo{ .image = m_eyeDepthImage, .viewType = vk::ImageViewType::e2D, .format = vk::Format::eD32Sfloat,
        .subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 } };
    m_eyeDepthView = vkDevice.createImageView(depthViewInfo).value;

    for (uint32 i = 0; i < 2; ++i)
    {
        vk::ImageCreateInfo colorInfo{
            .imageType = vk::ImageType::e2D, .format = colorFormat, .extent = { ext.width, ext.height, 1 },
            .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1, .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
            .sharingMode = vk::SharingMode::eExclusive, .initialLayout = vk::ImageLayout::eUndefined };
        Globals::gpuAllocator.createImage(colorInfo, m_eyeColorImage[i], m_eyeColorMem[i], "VR.eyeColor");
        vk::ImageViewCreateInfo colorViewInfo{ .image = m_eyeColorImage[i], .viewType = vk::ImageViewType::e2D, .format = colorFormat,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } };
        m_eyeColorView[i] = vkDevice.createImageView(colorViewInfo).value;
        std::array<vk::ImageView, 2> atts{ m_eyeColorView[i], m_eyeDepthView };
        vk::FramebufferCreateInfo fbInfo{ .renderPass = m_renderPass.getRenderPass(), .attachmentCount = (uint32)atts.size(), .pAttachments = atts.data(),
            .width = ext.width, .height = ext.height, .layers = 1 };
        m_eyeFramebuffer[i] = vkDevice.createFramebuffer(fbInfo).value;
    }
}

void Renderer::destroyEyeCompositeTargets()
{
    vk::Device vkDevice = Globals::device.getDevice();
    for (uint32 i = 0; i < 2; ++i)
    {
        if (m_eyeFramebuffer[i]) { vkDevice.destroyFramebuffer(m_eyeFramebuffer[i]); m_eyeFramebuffer[i] = nullptr; }
        if (m_eyeColorView[i]) { vkDevice.destroyImageView(m_eyeColorView[i]); m_eyeColorView[i] = nullptr; }
        if (m_eyeColorImage[i]) { Globals::gpuAllocator.destroyImage(m_eyeColorImage[i], m_eyeColorMem[i]); m_eyeColorImage[i] = nullptr; m_eyeColorMem[i] = nullptr; }
    }
    if (m_eyeDepthView) { vkDevice.destroyImageView(m_eyeDepthView); m_eyeDepthView = nullptr; }
    if (m_eyeDepthImage) { Globals::gpuAllocator.destroyImage(m_eyeDepthImage, m_eyeDepthMem); m_eyeDepthImage = nullptr; m_eyeDepthMem = nullptr; }
}

bool Renderer::recordGlobalIllum(uint32 frameIdx)
{
    PerFrameData& frameData = m_perFrameData[frameIdx];
    CommandBuffer& globalIllumCommandBuffer = frameData.globalIllumCommandBuffer;
    vk::CommandBufferInheritanceInfo globalIllumInheritanceInfo;
    vk::CommandBuffer vkGlobalIllumCommandBuffer = globalIllumCommandBuffer.begin(false, &globalIllumInheritanceInfo);
    auto fullBarrier = [&](vk::PipelineStageFlags2 srcStage, vk::AccessFlags2 srcAccess,
        vk::PipelineStageFlags2 dstStage, vk::AccessFlags2 dstAccess)
        {
            vk::MemoryBarrier2 bar{ .srcStageMask = srcStage, .srcAccessMask = srcAccess, .dstStageMask = dstStage, .dstAccessMask = dstAccess };
            vkGlobalIllumCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
        };

    // 1. Build BLASes for any meshes added since last frame (one-time per mesh).
    if (m_blasBuiltCount < m_meshInfoCounter)
    {
        const uint32 totalVertices = (uint32)(Globals::meshDataManager.getVertexBufUsed() / sizeof(RendererVKLayout::MeshVertex));
        m_accelStructure.recordBuildBlas(vkGlobalIllumCommandBuffer, Globals::meshDataManager.getVertexBuffer(), Globals::meshDataManager.getIndexBuffer(),
            m_meshInfosBuffer.getBackingStoreAs<RendererVKLayout::MeshInfo>().data(), m_blasBuiltCount, m_meshInfoCounter - m_blasBuiltCount, totalVertices);
        m_blasBuiltCount = m_meshInfoCounter;

        m_giProbePipeline.doClear();
    }

    // 2. One-time clear of the persistent probe table/SH (it accumulates across frames thereafter).
    if (m_giProbePipeline.needsClear())
    {
        m_giProbePipeline.recordClearPersistent(globalIllumCommandBuffer);
        m_giProbePipeline.markCleared();
    }

    // Make prior writes visible to the GI compute passes:
    //  - the light grid (compute storage writes) that the trace reuses to shade hits, and
    //  - the sun cascade shadow map depth writes (late/early fragment tests) that the trace samples.
    // The shadow render pass only synchronizes its depth writes to the FRAGMENT stage (for the main
    // pass); without this the compute trace samples the depth image with no dependency, which faults
    // NVIDIA (depth-compression metadata read from the wrong stage).
    fullBarrier(vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
        vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite | vk::AccessFlagBits2::eShaderSampledRead);

    // 3. Write the per-instance TLAS records on the GPU.
    const uint32 numInstances = std::min(m_meshInstanceCounter, m_maxGiTlasInstances);
    GIProbePipeline::TlasInstanceParams tlasParams{
        .renderNodeTransforms = frameData.inRenderNodeTransformsBuffer,
        .meshInstances = frameData.inMeshInstancesBuffer,
        .instanceOffsets = m_instanceOffsetsBuffer,
        .blasAddresses = m_accelStructure.getBlasAddressBuffer(),
        .materialInfos = m_materialInfosBuffer,
        .numInstances = numInstances,
    };
    m_giProbePipeline.recordTlasInstances(globalIllumCommandBuffer, frameIdx, tlasParams);

    // instance write -> TLAS build read. The build reads the instance buffer as SHADER_READ (AS_READ
    // covers the source acceleration structures, not the instance data), so the dst access must include it.
    fullBarrier(vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderRead);

    // 4. Rebuild this frame's TLAS (double-buffered). Note if the handle changed so the recorded-once AO pass
    // (which bakes the handle) can be re-recorded.
    const bool tlasHandleChanged = m_accelStructure.recordBuildTlas(vkGlobalIllumCommandBuffer, frameIdx, m_giProbePipeline.getTlasInstanceBuffer(frameIdx), numInstances);

    // TLAS build -> ray-query read (GI/AO compute, and the forward fragment pass for RT light shadows)
    fullBarrier(vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
        vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eAccelerationStructureReadKHR | vk::AccessFlagBits2::eShaderStorageRead);

    // 5. Trace rays per clipmap probe and temporally blend irradiance into the SH. The probe set and
    // its toroidal window are derived from the camera (this frame's u_viewPos in the UBO); probes that
    // scrolled in since last frame (relative to m_giPrevCameraPos) are full-replaced rather than blended.
    GIProbePipeline::TraceParams traceParams{
        .ubo = frameData.ubo,
        .lightInfos = frameData.lightInfosBuffer,
        .lightGrid = frameData.lightGridsBuffer,
        .lightTable = frameData.lightTableBuffer,
        .vertexBuffer = Globals::meshDataManager.getVertexBuffer(),
        .indexBuffer = Globals::meshDataManager.getIndexBuffer(),
        .meshInfos = m_meshInfosBuffer,
        .meshInstances = frameData.inMeshInstancesBuffer,
        .materialInfos = m_materialInfosBuffer,
        .tlas = m_accelStructure.getTlas(frameIdx),
        .shadowMapView = frameData.shadowMap.getSampleView(),
        .shadowMapSampler = frameData.shadowMap.getSampler(),
        .frameIndex = m_frameCounter,
        .prevViewPos = m_giPrevCameraPos,
    };
    m_giProbePipeline.recordTrace(globalIllumCommandBuffer, frameIdx, traceParams);
    m_giPrevCameraPos = m_cameraPos;

    // trace (SH write) -> fragment read in the main pass
    fullBarrier(vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite,
        vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderStorageRead);

    globalIllumCommandBuffer.end();
    return tlasHandleChanged;
}

void Renderer::recordCommandBuffers()
{
    const uint32 frameIdx = m_swapChain.getCurrentFrameIndex();
    PerFrameData& frameData = m_perFrameData[frameIdx];

    const bool recordScene = !frameData.updated && m_meshInstanceCounter > 0;
    if (recordScene)
    {
        recordIndirectCull(frameIdx);
        recordLightGrid(frameIdx);
        recordShadowCull(frameIdx);
        recordShadowDraw(frameIdx);
        recordVolumetricFog(frameIdx); // shared scatter/integrate (center view in VR)
        recordEyeAdaptation(frameIdx); // shared (samples the left eye's resolved colour in VR)
        // Composite secondary draws into the desktop swapchain (the left eye / TAA-resolved colour); in VR
        // it's the desktop-window mirror, so it's recorded in both modes.
        recordComposite(frameIdx);
        // The remaining per-eye screen-space passes (gbuffer, AO, forward, fog apply, TAA) are recorded
        // inline in the primary below in VR; on desktop they stay cached secondaries (one eye).
        if (m_sceneViewCount == 1)
        {
            recordStaticMesh(frameIdx);
            recordGBuffer(frameIdx);
            recordGiProbeDebug(frameIdx);
            recordAO(frameIdx);
            recordFogApply(frameIdx);
            recordTaa(frameIdx);
        }
        frameData.updated = true;
    }

    if (m_meshInstanceCounter > 0 && recordGlobalIllum(frameIdx))
    {
        if (m_sceneViewCount == 1)
            recordAO(frameIdx);
        recordVolumetricFog(frameIdx);
    }

    // Live tunables + delta time into the mapped params buffer (no command-buffer re-record needed).
    if (m_meshInstanceCounter > 0)
    {
        static Clock::time_point lastTime = Clock::now();
        const Clock::time_point now = Clock::now();
        const float deltaSeconds = std::min(std::chrono::duration<float>(now - lastTime).count(), 0.25f);
        lastTime = now;
        m_eyeAdaptationPipeline.updateParams(frameIdx, m_postParams, deltaSeconds);
    }

    vk::CommandBuffer vkIndirectCullCommandBuffer = frameData.indirectCullCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkLightGridCommandBuffer = frameData.lightGridCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkStaticMeshCommandBuffer = frameData.staticMeshCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkGbufferCommandBuffer = frameData.gbufferCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkShadowCullCommandBuffer = frameData.shadowCullCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkShadowDrawCommandBuffer = frameData.shadowDrawCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkGlobalIllumCommandBuffer = frameData.globalIllumCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkAoCommandBuffer = frameData.aoCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkVolumetricFogCommandBuffer = frameData.volumetricFogCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkFogApplyCommandBuffer = frameData.fogApplyCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkGiProbeDebugCommandBuffer = frameData.giProbeDebugCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkTaaCommandBuffer = frameData.taaCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkEyeAdaptCommandBuffer = frameData.eyeAdaptCommandBuffer.getCommandBuffer();
    vk::CommandBuffer vkCompositeCommandBuffer = frameData.compositeCommandBuffer.getCommandBuffer();

    vk::CommandBufferInheritanceInfo inheritance{ .renderPass = m_renderPass.getRenderPass() };
    vk::CommandBuffer vkImguiCommandBuffer = frameData.imguiCommandBuffer.begin(true, &inheritance);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkImguiCommandBuffer, nullptr);
    frameData.imguiCommandBuffer.end();

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

    if (m_meshInstanceCounter > 0)
    {
        vkCommandBuffer.executeCommands(1, &vkIndirectCullCommandBuffer);
        vkCommandBuffer.executeCommands(1, &vkLightGridCommandBuffer);
        // RT sun shadows replace the cascades entirely (forward pass traces, GI uses per-probe sun rays),
        // so skip the shadow cull + cascade render. The primary CB is re-recorded every frame, so the
        // toggle takes effect immediately; the cached secondary CBs just go unexecuted.
        if (!m_rtParams.rtSunShadow)
        {
            vkCommandBuffer.executeCommands(1, &vkShadowCullCommandBuffer);

            ShadowMap& shadowMap = frameData.shadowMap;
            vk::ClearValue shadowClear;
            shadowClear.depthStencil = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 };
            const vk::RenderPassBeginInfo shadowRpBegin{
                .renderPass = shadowMap.getRenderPass(),
                .framebuffer = shadowMap.getFramebuffer(),
                .renderArea = vk::Rect2D{.offset = vk::Offset2D{ 0, 0 }, .extent = vk::Extent2D{ shadowMap.getResolution(), shadowMap.getResolution() } },
                .clearValueCount = 1,
                .pClearValues = &shadowClear,
            };
            vkCommandBuffer.beginRenderPass(shadowRpBegin, vk::SubpassContents::eSecondaryCommandBuffers);
            vkCommandBuffer.executeCommands(1, &vkShadowDrawCommandBuffer);
            vkCommandBuffer.endRenderPass();
        }
        SceneColor& sceneColor = frameData.sceneColor;
        GBuffer& gbuffer = frameData.gbuffer;
        std::array<vk::ClearValue, 2> gbufferClears{ vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f } }, vk::ClearDepthStencilValue{ 1.0f, 0 } };
        const vk::Rect2D gbufferArea{ .offset = vk::Offset2D{ 0, 0 }, .extent = vk::Extent2D{ gbuffer.getWidth(), gbuffer.getHeight() } };
        std::array<vk::ClearValue, 2> sceneClears{ vk::ClearColorValue{ std::array<float, 4>{ 0.5f, 0.7f, 0.9f, 1.0f } }, vk::ClearDepthStencilValue{ 1.0f, 0 } };
        const vk::Rect2D sceneArea{ .offset = vk::Offset2D{ m_viewportRect.min.x, m_viewportRect.min.y }, .extent = vk::Extent2D{ sceneColor.getWidth() - m_viewportRect.min.x, sceneColor.getHeight() - m_viewportRect.min.y } };
        const vk::AccelerationStructureKHR tlas = m_accelStructure.getTlas(frameIdx);

        if (m_sceneViewCount > 1)
        {
            // ---- VR: per-eye screen-space chain (gbuffer -> AO -> forward+fog -> TAA), recorded inline ----
            // GI (TLAS build + probe trace) and fog scatter/integrate are shared (built once for the centre
            // view); each eye's gbuffer/AO/forward/TAA then runs against its own images.
            vkCommandBuffer.executeCommands(1, &vkGlobalIllumCommandBuffer);
            if (m_fogParams.enabled)
                vkCommandBuffer.executeCommands(1, &vkVolumetricFogCommandBuffer);

            for (uint32 eye = 0; eye < m_sceneViewCount; ++eye)
            {
                { // G-buffer prepass for this eye (layer eye)
                    const vk::RenderPassBeginInfo gbufferRpBegin{
                        .renderPass = gbuffer.getRenderPass(),
                        .framebuffer = gbuffer.getFramebuffer(eye),
                        .renderArea = gbufferArea,
                        .clearValueCount = (uint32)gbufferClears.size(),
                        .pClearValues = gbufferClears.data(),
                    };
                    vkCommandBuffer.beginRenderPass(gbufferRpBegin, vk::SubpassContents::eInline);
                    if (m_meshInfoCounter > 0)
                        recordGBufferInto(commandBuffer, frameIdx, eye);
                    vkCommandBuffer.endRenderPass();
                }
                if (m_rtaoParams.enabled)
                    recordAOInto(commandBuffer, frameIdx, eye); // compute AO for this eye

                // This eye's forward descriptor set takes this eye's AO + the (per-frame) TLAS.
                m_staticMeshGraphicsPipeline.updateAODescriptor(frameData.staticMeshPipelineDescriptorSet[eye].getDescriptorSet(), m_rtaoPipeline.getAOView(frameIdx, eye), m_rtaoPipeline.getAOSampler());
                if (tlas)
                    m_staticMeshGraphicsPipeline.updateTlasDescriptor(frameData.staticMeshPipelineDescriptorSet[eye].getDescriptorSet(), tlas);

                { // Forward (+ fog apply) into this eye's SceneColor layer
                    const vk::RenderPassBeginInfo eyeRpBegin{
                        .renderPass = sceneColor.getRenderPass(),
                        .framebuffer = sceneColor.getFramebuffer(eye),
                        .renderArea = sceneArea,
                        .clearValueCount = (uint32)sceneClears.size(),
                        .pClearValues = sceneClears.data(),
                    };
                    vkCommandBuffer.beginRenderPass(eyeRpBegin, vk::SubpassContents::eInline);
                    recordStaticMeshInto(commandBuffer, frameIdx, eye);
                    if (m_fogParams.enabled)
                        recordFogApplyInto(commandBuffer, frameIdx, eye);
                    vkCommandBuffer.endRenderPass();
                }

                vk::MemoryBarrier2 colorToTaa{ // scene colour -> TAA compute sampled read
                    .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                    .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                    .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
                };
                vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &colorToTaa });
                recordTaaInto(commandBuffer, frameIdx, eye); // resolve into this eye's history
            }

            // Eye adaptation samples the left eye's resolved colour (shared exposure, no per-eye flicker).
            vkCommandBuffer.executeCommands(1, &vkEyeAdaptCommandBuffer);

            // Tonemap each eye's TAA-resolved colour into its LDR composite target (copied into the OpenXR
            // eye swapchains in present()). TAA left the resolved images in GENERAL with a write->read barrier.
            const vk::Extent2D ext = m_swapChain.getLayout().extent;
            for (uint32 eye = 0; eye < 2; ++eye)
            {
                std::array<vk::ClearValue, 2> eyeClears{ vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } }, vk::ClearDepthStencilValue{ 1.0f, 0 } };
                const vk::RenderPassBeginInfo eyeCompositeBegin{
                    .renderPass = m_renderPass.getRenderPass(),
                    .framebuffer = m_eyeFramebuffer[eye],
                    .renderArea = vk::Rect2D{ .offset = vk::Offset2D{ 0, 0 }, .extent = ext },
                    .clearValueCount = (uint32)eyeClears.size(),
                    .pClearValues = eyeClears.data(),
                };
                vkCommandBuffer.beginRenderPass(eyeCompositeBegin, vk::SubpassContents::eInline);
                vkCommandBuffer.setViewport(0, vk::Viewport{ .x = 0.0f, .y = 0.0f, .width = (float)ext.width, .height = (float)ext.height, .minDepth = 0.0f, .maxDepth = 1.0f });
                vkCommandBuffer.setScissor(0, vk::Rect2D{ .offset = vk::Offset2D{ 0, 0 }, .extent = ext });
                CompositePipeline::RecordParams eyeComposite{
                    .descriptorSet = m_vrCompositeDescriptorSet[eye],
                    .resolvedView = m_taaPipeline.getResolvedView(frameIdx, eye),
                    .resolvedLayout = vk::ImageLayout::eGeneral,
                    .sampler = m_taaPipeline.getSampler(),
                    .exposureBuffer = m_eyeAdaptationPipeline.getExposureBuffer().getBuffer(),
                    .exposureEV = m_postParams.exposureEV,
                    .tonemapper = m_postParams.tonemapper,
                    .autoExposure = m_postParams.autoExposure ? 1 : 0,
                };
                m_compositePipeline.record(commandBuffer, eyeComposite);
                vkCommandBuffer.endRenderPass();
            }
        }
        else
        {
            { // Depth + world-normal G-buffer prepass (camera view)
                const vk::RenderPassBeginInfo gbufferRpBegin{
                    .renderPass = gbuffer.getRenderPass(),
                    .framebuffer = gbuffer.getFramebuffer(),
                    .renderArea = gbufferArea,
                    .clearValueCount = (uint32)gbufferClears.size(),
                    .pClearValues = gbufferClears.data(),
                };
                vkCommandBuffer.beginRenderPass(gbufferRpBegin, vk::SubpassContents::eSecondaryCommandBuffers);
                if (m_meshInfoCounter > 0)
                    vkCommandBuffer.executeCommands(1, &vkGbufferCommandBuffer);
                vkCommandBuffer.endRenderPass();
            }
            vkCommandBuffer.executeCommands(1, &vkGlobalIllumCommandBuffer);
            if (m_rtaoParams.enabled)
                vkCommandBuffer.executeCommands(1, &vkAoCommandBuffer);
            // Fog scatter/integrate compute; the primary is re-recorded every frame, so the enable toggle
            // takes effect immediately (the integrated grid was cleared to "no fog" at init when disabled).
            if (m_fogParams.enabled)
                vkCommandBuffer.executeCommands(1, &vkVolumetricFogCommandBuffer);
            m_staticMeshGraphicsPipeline.updateAODescriptor(frameData.staticMeshPipelineDescriptorSet[0].getDescriptorSet(), m_rtaoPipeline.getAOView(frameIdx, 0), m_rtaoPipeline.getAOSampler());
            if (tlas)
                m_staticMeshGraphicsPipeline.updateTlasDescriptor(frameData.staticMeshPipelineDescriptorSet[0].getDescriptorSet(), tlas);

            const vk::RenderPassBeginInfo sceneRpBegin{
                .renderPass = sceneColor.getRenderPass(),
                .framebuffer = sceneColor.getFramebuffer(),
                .renderArea = sceneArea,
                .clearValueCount = (uint32)sceneClears.size(),
                .pClearValues = sceneClears.data(),
            };
            vkCommandBuffer.beginRenderPass(sceneRpBegin, vk::SubpassContents::eSecondaryCommandBuffers);
            vkCommandBuffer.executeCommands(1, &vkStaticMeshCommandBuffer);
            if (m_giProbeDebugEnabled)
                vkCommandBuffer.executeCommands(1, &vkGiProbeDebugCommandBuffer);
            if (m_fogParams.enabled)
                vkCommandBuffer.executeCommands(1, &vkFogApplyCommandBuffer);
            vkCommandBuffer.endRenderPass();

            vk::MemoryBarrier2 bar{ // color -> TAA barrier
                .srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderSampledRead,
            };
            vkCommandBuffer.pipelineBarrier2(vk::DependencyInfo{ .memoryBarrierCount = 1, .pMemoryBarriers = &bar });
            vkCommandBuffer.executeCommands(1, &vkTaaCommandBuffer);
            // Eye adaptation: reads the resolved colour (TAA barrier above), writes the exposure the composite reads.
            vkCommandBuffer.executeCommands(1, &vkEyeAdaptCommandBuffer);
        }
    }

    // Swapchain render pass: composite the resolved scene into the swapchain, then ImGui on top.
    constexpr std::array<vk::ClearValue, 2> clearValues{ vk::ClearColorValue{ std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 0.0f } }, vk::ClearDepthStencilValue{ 1.0f, 0 } };
    const vk::RenderPassBeginInfo renderPassBeginInfo{
        .renderPass = m_renderPass.getRenderPass(),
        .framebuffer = m_framebuffers.getFramebuffer(m_swapChain.getCurrentImageIdx()),
        .renderArea = vk::Rect2D { .offset = vk::Offset2D { 0, 0 }, .extent = m_swapChain.getLayout().extent },
        .clearValueCount = (uint32)clearValues.size(),
        .pClearValues = clearValues.data(),
    };
    vkCommandBuffer.beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eSecondaryCommandBuffers);
    if (m_meshInstanceCounter > 0)
        vkCommandBuffer.executeCommands(1, &vkCompositeCommandBuffer);
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

    m_meshInfosBuffer.appendToBackingStore<RendererVKLayout::MeshInfo>(meshInfos);

    if (m_meshInfoCounter > m_maxUniqueMeshes)
        growUniqueMeshCapacity(m_meshInfoCounter); // re-uploads the full CPU copy
    else
        m_meshInfosBuffer.upload(meshInfos.size() * sizeof(RendererVKLayout::MeshInfo),
            meshInfos.data(), baseMeshInfoIdx * sizeof(RendererVKLayout::MeshInfo));

    setHaveToRecordCommandBuffers();
    return baseMeshInfoIdx;
}

uint32 Renderer::addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos)
{
    const uint32 baseMaterialInfoIdx = m_materialInfoCounter;
    m_materialInfoCounter += (uint32)materialInfos.size();
    assert(m_materialInfoCounter < USHRT_MAX);

    m_materialInfosBuffer.appendToBackingStore<RendererVKLayout::MaterialInfo>(materialInfos);

    if (m_materialInfoCounter > m_maxUniqueMaterials)
        growMaterialCapacity(m_materialInfoCounter); // re-uploads the full CPU copy
    else
        m_materialInfosBuffer.upload(materialInfos.size() * sizeof(RendererVKLayout::MaterialInfo),
            materialInfos.data(), baseMaterialInfoIdx * sizeof(RendererVKLayout::MaterialInfo));

    setHaveToRecordCommandBuffers();
    return baseMaterialInfoIdx;
}

uint32 Renderer::addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets)
{
    const uint32 baseInstanceOffsetIdx = m_instanceOffsetCounter;
    m_instanceOffsetCounter += (uint32)meshInstanceOffsets.size();

    m_instanceOffsetsBuffer.appendToBackingStore<RendererVKLayout::MeshInstanceOffset>(meshInstanceOffsets);

    if (m_instanceOffsetCounter > m_maxInstanceOffsets)
        growInstanceOffsetCapacity(m_instanceOffsetCounter); // re-uploads the full CPU copy
    else
        m_instanceOffsetsBuffer.upload(meshInstanceOffsets.size() * sizeof(RendererVKLayout::MeshInstanceOffset),
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

Stats Renderer::getStats()
{
    Stats stats;

    stats.numLights = m_lightCounter;
    stats.maxLights = RendererVKLayout::MAX_LIGHTS;

    stats.numMeshInstances = m_meshInstanceCounter;
    stats.maxMeshInstances = m_maxInstanceData;

    stats.numInstanceOffsets = m_instanceOffsetCounter;
    stats.maxInstanceOffsets = m_maxInstanceOffsets;

    stats.numMeshTypes = m_meshInfoCounter;
    stats.maxMeshTypes = m_maxUniqueMeshes;

    stats.numMaterials = m_materialInfoCounter;
    stats.maxMaterials = m_maxUniqueMaterials;

    stats.numRenderNodes = (uint32)m_renderNodeTransforms.size();
    stats.maxRenderNodes = m_maxRenderNodes;

    stats.numTextures = (uint32)Globals::textureManager.getNumTextures();
	stats.maxTextures = Globals::textureManager.getMaxTextures();

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
    stats.maxLightGrids = m_lightTableEntries / 2; // We don't want to go over 50% because of hash table collisions
    stats.lightGridMemUsageBytes = info.inout_gridDataCounter * sizeof(uint32);
    stats.maxLightGridMemUsageBytes = m_lightGridBufferSize;

    const Allocator::MemoryUsage gpuMem = Globals::gpuAllocator.getMemoryUsage();
    stats.gpuMemoryUsedBytes = gpuMem.usedBytes;
    stats.gpuMemoryReservedBytes = gpuMem.reservedBytes;
    stats.gpuMemoryBudgetBytes = gpuMem.budgetBytes;

    return stats;
}
