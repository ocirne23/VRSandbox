module;

#include <vulkan/vulkan_core.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

module RendererVK:OpenXRSession;

import Core;
import :VK;

namespace
{
    const char* xrResultStr(XrResult r)
    {
        switch (r)
        {
        case XR_SUCCESS: return "XR_SUCCESS";
        case XR_ERROR_VALIDATION_FAILURE: return "XR_ERROR_VALIDATION_FAILURE";
        case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
        case XR_ERROR_INSTANCE_LOST: return "XR_ERROR_INSTANCE_LOST";
        case XR_ERROR_SESSION_LOST: return "XR_ERROR_SESSION_LOST";
        case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
        case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
        case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
        case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
        default: return "XR_ERROR_<other>";
        }
    }

    // Returns true on success; logs and returns false otherwise.
    bool xrCheck(XrResult r, const char* what)
    {
        if (XR_SUCCEEDED(r))
            return true;
        printf("OpenXR: %s failed: %s (%d)\n", what, xrResultStr(r), (int)r);
        return false;
    }

    std::vector<std::string> tokenize(const std::string& s)
    {
        std::vector<std::string> out;
        size_t start = 0;
        while (start < s.size())
        {
            size_t end = s.find(' ', start);
            if (end == std::string::npos) end = s.size();
            if (end > start)
                out.emplace_back(s.substr(start, end - start));
            start = end + 1;
        }
        return out;
    }
}

bool OpenXRSession::initInstanceAndSystem()
{
    const char* enabledExtensions[] = { XR_KHR_VULKAN_ENABLE_EXTENSION_NAME };

    auto copyFixed = [](char* dst, size_t cap, const char* src)
    {
        size_t i = 0;
        for (; src[i] && i + 1 < cap; ++i) dst[i] = src[i];
        dst[i] = '\0';
    };

    XrInstanceCreateInfo createInfo{ XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    copyFixed(createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "App");
    copyFixed(createInfo.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "VRSandbox");
    createInfo.enabledExtensionCount = (uint32)(sizeof(enabledExtensions) / sizeof(enabledExtensions[0]));
    createInfo.enabledExtensionNames = enabledExtensions;

    XrResult r = xrCreateInstance(&createInfo, &m_instance);
    if (!XR_SUCCEEDED(r))
    {
        // No runtime installed / no HMD: this is an expected fallback, not a hard error.
        printf("OpenXR: xrCreateInstance failed (%s) - falling back to desktop-only.\n", xrResultStr(r));
        m_instance = XR_NULL_HANDLE;
        return false;
    }

    if (!loadKhrFunctions())
    {
        destroy();
        return false;
    }

    XrSystemGetInfo systemInfo{ XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(m_instance, &systemInfo, &m_systemId);
    if (!XR_SUCCEEDED(r))
    {
        printf("OpenXR: xrGetSystem failed (%s) - is the headset connected?\n", xrResultStr(r));
        destroy();
        return false;
    }

    // The runtime requires the graphics-requirements query before the device is chosen / the session
    // is created (validation enforces it). The min/max Vulkan API version it reports is informational
    // here; the engine targets 1.3 which every desktop VR runtime supports.
    XrGraphicsRequirementsVulkanKHR gfxReq{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
    if (!xrCheck(m_pfnGetVulkanGraphicsRequirements(m_instance, m_systemId, &gfxReq), "xrGetVulkanGraphicsRequirementsKHR"))
    {
        destroy();
        return false;
    }

    uint32 viewCount = 0;
    if (!xrCheck(xrEnumerateViewConfigurationViews(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr), "xrEnumerateViewConfigurationViews(count)"))
    {
        destroy();
        return false;
    }
    if (viewCount != VIEW_COUNT)
    {
        printf("OpenXR: expected %u stereo views, runtime reports %u\n", VIEW_COUNT, viewCount);
        destroy();
        return false;
    }
    std::vector<XrViewConfigurationView> views(viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    if (!xrCheck(xrEnumerateViewConfigurationViews(m_instance, m_systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data()), "xrEnumerateViewConfigurationViews"))
    {
        destroy();
        return false;
    }
    m_viewConfig = views[0]; // both eyes share the recommended dimensions in practice

    XrSystemProperties sysProps{ XR_TYPE_SYSTEM_PROPERTIES };
    if (XR_SUCCEEDED(xrGetSystemProperties(m_instance, m_systemId, &sysProps)))
        printf("OpenXR: system '%s', recommended eye target %ux%u\n", sysProps.systemName,
            m_viewConfig.recommendedImageRectWidth, m_viewConfig.recommendedImageRectHeight);

    m_enabled = true;
    return true;
}

bool OpenXRSession::loadKhrFunctions()
{
    auto load = [&](const char* name, PFN_xrVoidFunction* out) -> bool
    {
        XrResult r = xrGetInstanceProcAddr(m_instance, name, out);
        return xrCheck(r, name);
    };
    bool ok = true;
    ok &= load("xrGetVulkanInstanceExtensionsKHR", (PFN_xrVoidFunction*)&m_pfnGetVulkanInstanceExtensions);
    ok &= load("xrGetVulkanDeviceExtensionsKHR", (PFN_xrVoidFunction*)&m_pfnGetVulkanDeviceExtensions);
    ok &= load("xrGetVulkanGraphicsDeviceKHR", (PFN_xrVoidFunction*)&m_pfnGetVulkanGraphicsDevice);
    ok &= load("xrGetVulkanGraphicsRequirementsKHR", (PFN_xrVoidFunction*)&m_pfnGetVulkanGraphicsRequirements);
    return ok;
}

std::vector<std::string> OpenXRSession::getRequiredVulkanInstanceExtensions() const
{
    uint32 size = 0;
    if (!xrCheck(m_pfnGetVulkanInstanceExtensions(m_instance, m_systemId, 0, &size, nullptr), "xrGetVulkanInstanceExtensionsKHR(size)"))
        return {};
    std::string buffer(size, '\0');
    if (!xrCheck(m_pfnGetVulkanInstanceExtensions(m_instance, m_systemId, size, &size, buffer.data()), "xrGetVulkanInstanceExtensionsKHR"))
        return {};
    if (!buffer.empty() && buffer.back() == '\0') buffer.pop_back();
    return tokenize(buffer);
}

std::vector<std::string> OpenXRSession::getRequiredVulkanDeviceExtensions() const
{
    uint32 size = 0;
    if (!xrCheck(m_pfnGetVulkanDeviceExtensions(m_instance, m_systemId, 0, &size, nullptr), "xrGetVulkanDeviceExtensionsKHR(size)"))
        return {};
    std::string buffer(size, '\0');
    if (!xrCheck(m_pfnGetVulkanDeviceExtensions(m_instance, m_systemId, size, &size, buffer.data()), "xrGetVulkanDeviceExtensionsKHR"))
        return {};
    if (!buffer.empty() && buffer.back() == '\0') buffer.pop_back();
    return tokenize(buffer);
}

vk::PhysicalDevice OpenXRSession::getVulkanGraphicsDevice(vk::Instance vkInstance) const
{
    VkPhysicalDevice physDevice = VK_NULL_HANDLE;
    if (!xrCheck(m_pfnGetVulkanGraphicsDevice(m_instance, m_systemId, (VkInstance)vkInstance, &physDevice), "xrGetVulkanGraphicsDeviceKHR"))
        return nullptr;
    return vk::PhysicalDevice(physDevice);
}

int64_t OpenXRSession::selectSwapchainFormat() const
{
    uint32 count = 0;
    if (!xrCheck(xrEnumerateSwapchainFormats(m_session, 0, &count, nullptr), "xrEnumerateSwapchainFormats(count)"))
        return 0;
    std::vector<int64_t> formats(count);
    if (!xrCheck(xrEnumerateSwapchainFormats(m_session, count, &count, formats.data()), "xrEnumerateSwapchainFormats"))
        return 0;

    // Composite output is LDR post-tonemap, so prefer an 8-bit sRGB format.
    const int64_t preferred[] = { VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB };
    for (int64_t want : preferred)
        for (int64_t have : formats)
            if (have == want)
                return have;
    return formats.empty() ? 0 : formats[0];
}

bool OpenXRSession::createSession(vk::Instance vkInstance, vk::PhysicalDevice vkPhysicalDevice, vk::Device vkDevice,
    uint32 graphicsQueueFamily, uint32 graphicsQueueIndex)
{
    m_vkDevice = (VkDevice)vkDevice;
    m_graphicsQueueFamily = graphicsQueueFamily;
    vkGetDeviceQueue(m_vkDevice, graphicsQueueFamily, graphicsQueueIndex, &m_vkQueue);

    XrGraphicsBindingVulkanKHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
    binding.instance = (VkInstance)vkInstance;
    binding.physicalDevice = (VkPhysicalDevice)vkPhysicalDevice;
    binding.device = m_vkDevice;
    binding.queueFamilyIndex = graphicsQueueFamily;
    binding.queueIndex = graphicsQueueIndex;

    XrSessionCreateInfo sessionInfo{ XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next = &binding;
    sessionInfo.systemId = m_systemId;
    if (!xrCheck(xrCreateSession(m_instance, &sessionInfo, &m_session), "xrCreateSession"))
        return false;

    XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f; // identity
    if (!xrCheck(xrCreateReferenceSpace(m_session, &spaceInfo, &m_space), "xrCreateReferenceSpace"))
        return false;

    m_swapchainFormat = selectSwapchainFormat();

    // One single-layer swapchain per eye: the SteamVR Vulkan runtime silently ignores arraySize>1.
    for (uint32 eye = 0; eye < VIEW_COUNT; ++eye)
    {
        XrSwapchainCreateInfo scInfo{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        scInfo.format = m_swapchainFormat;
        scInfo.sampleCount = 1;
        scInfo.width = m_viewConfig.recommendedImageRectWidth;
        scInfo.height = m_viewConfig.recommendedImageRectHeight;
        scInfo.faceCount = 1;
        scInfo.arraySize = 1;
        scInfo.mipCount = 1;
        if (!xrCheck(xrCreateSwapchain(m_session, &scInfo, &m_swapchains[eye]), "xrCreateSwapchain"))
            return false;

        uint32 imageCount = 0;
        if (!xrCheck(xrEnumerateSwapchainImages(m_swapchains[eye], 0, &imageCount, nullptr), "xrEnumerateSwapchainImages(count)"))
            return false;
        m_swapchainImages[eye].assign(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
        if (!xrCheck(xrEnumerateSwapchainImages(m_swapchains[eye], imageCount, &imageCount,
            (XrSwapchainImageBaseHeader*)m_swapchainImages[eye].data()), "xrEnumerateSwapchainImages"))
            return false;
    }

    // Own command pool + a single reusable clear buffer (decoupled from the engine's pool). M1
    // serializes with vkQueueWaitIdle after each submit, so one buffer is enough.
    VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily;
    if (vkCreateCommandPool(m_vkDevice, &poolInfo, nullptr, &m_vkCommandPool) != VK_SUCCESS)
    {
        printf("OpenXR: failed to create command pool\n");
        return false;
    }
    VkCommandBufferAllocateInfo cbInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cbInfo.commandPool = m_vkCommandPool;
    cbInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_vkDevice, &cbInfo, &m_clearCommandBuffer) != VK_SUCCESS)
    {
        printf("OpenXR: failed to allocate command buffers\n");
        return false;
    }

    printf("OpenXR: session created, 2x%ux%u eye swapchains\n",
        m_viewConfig.recommendedImageRectWidth, m_viewConfig.recommendedImageRectHeight);
    return true;
}

void OpenXRSession::pollEvents()
{
    if (!m_enabled)
        return;

    XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
    while (true)
    {
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        XrResult r = xrPollEvent(m_instance, &event);
        if (r == XR_EVENT_UNAVAILABLE)
            break;
        if (!XR_SUCCEEDED(r))
            break;

        switch (event.type)
        {
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            m_exitRequested = true;
            break;
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
        {
            auto& stateEvent = *reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            m_sessionState = stateEvent.state;
            switch (m_sessionState)
            {
            case XR_SESSION_STATE_READY:
            {
                XrSessionBeginInfo beginInfo{ XR_TYPE_SESSION_BEGIN_INFO };
                beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (xrCheck(xrBeginSession(m_session, &beginInfo), "xrBeginSession"))
                    m_sessionRunning = true;
                break;
            }
            case XR_SESSION_STATE_STOPPING:
                xrCheck(xrEndSession(m_session), "xrEndSession");
                m_sessionRunning = false;
                break;
            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                m_exitRequested = true;
                m_sessionRunning = false;
                break;
            default:
                break;
            }
            break;
        }
        default:
            break;
        }
    }
}

void OpenXRSession::renderClearFrame()
{
    if (!m_sessionRunning)
        return;

    XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frameState{ XR_TYPE_FRAME_STATE };
    if (!xrCheck(xrWaitFrame(m_session, &waitInfo, &frameState), "xrWaitFrame"))
        return;

    XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
    if (!xrCheck(xrBeginFrame(m_session, &beginInfo), "xrBeginFrame"))
        return;

    XrCompositionLayerProjectionView projViews[VIEW_COUNT]{};
    XrCompositionLayerProjection projLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    const XrCompositionLayerBaseHeader* layers[1] = { nullptr };
    uint32 layerCount = 0;

    if (frameState.shouldRender)
    {
        XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = m_space;
        XrViewState viewState{ XR_TYPE_VIEW_STATE };
        XrView xrViews[VIEW_COUNT]{ { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
        uint32 located = 0;
        xrCheck(xrLocateViews(m_session, &locateInfo, &viewState, VIEW_COUNT, &located, xrViews), "xrLocateViews");

        // Animate the clear colour so it's obvious the headset is live.
        const float t = (float)(m_frameCounter % 256) / 255.0f;
        VkClearColorValue clearColor{};
        clearColor.float32[0] = t;
        clearColor.float32[1] = 0.2f;
        clearColor.float32[2] = 1.0f - t;
        clearColor.float32[3] = 1.0f;

        bool allEyesOk = true;
        for (uint32 eye = 0; eye < VIEW_COUNT; ++eye)
        {
            uint32 imageIndex = 0;
            XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            if (!xrCheck(xrAcquireSwapchainImage(m_swapchains[eye], &acquireInfo, &imageIndex), "xrAcquireSwapchainImage"))
            {
                allEyesOk = false;
                break;
            }
            XrSwapchainImageWaitInfo scWaitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            scWaitInfo.timeout = XR_INFINITE_DURATION;
            if (xrCheck(xrWaitSwapchainImage(m_swapchains[eye], &scWaitInfo), "xrWaitSwapchainImage"))
            {
                VkCommandBuffer cb = m_clearCommandBuffer;
                VkCommandBufferBeginInfo cbBegin{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
                cbBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                vkBeginCommandBuffer(cb, &cbBegin);

                VkImage image = m_swapchainImages[eye][imageIndex].image;
                VkImageSubresourceRange range{};
                range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                range.levelCount = 1;
                range.layerCount = 1;

                VkImageMemoryBarrier toDst{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                toDst.srcAccessMask = 0;
                toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toDst.image = image;
                toDst.subresourceRange = range;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &toDst);

                vkCmdClearColorImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

                VkImageMemoryBarrier toColor{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                toColor.image = image;
                toColor.subresourceRange = range;
                vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &toColor);

                vkEndCommandBuffer(cb);

                VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cb;
                vkQueueSubmit(m_vkQueue, 1, &submit, VK_NULL_HANDLE);
                vkQueueWaitIdle(m_vkQueue); // M1: simple, correct; replaced with proper sync later
            }

            XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrCheck(xrReleaseSwapchainImage(m_swapchains[eye], &releaseInfo), "xrReleaseSwapchainImage");

            projViews[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projViews[eye].pose = xrViews[eye].pose;
            projViews[eye].fov = xrViews[eye].fov;
            projViews[eye].subImage.swapchain = m_swapchains[eye];
            projViews[eye].subImage.imageArrayIndex = 0;
            projViews[eye].subImage.imageRect.offset = { 0, 0 };
            projViews[eye].subImage.imageRect.extent = {
                (int32_t)m_viewConfig.recommendedImageRectWidth,
                (int32_t)m_viewConfig.recommendedImageRectHeight };
        }

        if (allEyesOk)
        {
            projLayer.space = m_space;
            projLayer.viewCount = VIEW_COUNT;
            projLayer.views = projViews;
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer);
            layerCount = 1;
        }
    }

    XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layers;
    xrCheck(xrEndFrame(m_session, &endInfo), "xrEndFrame");

    m_frameCounter++;
}

void OpenXRSession::destroy()
{
    if (m_vkDevice != VK_NULL_HANDLE && m_vkCommandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);
        m_vkCommandPool = VK_NULL_HANDLE;
    }
    m_clearCommandBuffer = VK_NULL_HANDLE;
    for (uint32 eye = 0; eye < VIEW_COUNT; ++eye)
    {
        if (m_swapchains[eye] != XR_NULL_HANDLE)
        {
            xrDestroySwapchain(m_swapchains[eye]);
            m_swapchains[eye] = XR_NULL_HANDLE;
        }
        m_swapchainImages[eye].clear();
    }
    if (m_space != XR_NULL_HANDLE)
    {
        xrDestroySpace(m_space);
        m_space = XR_NULL_HANDLE;
    }
    if (m_session != XR_NULL_HANDLE)
    {
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }
    if (m_instance != XR_NULL_HANDLE)
    {
        xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }
    m_enabled = false;
    m_sessionRunning = false;
}
