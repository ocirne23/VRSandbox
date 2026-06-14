module;

// OpenXR + Vulkan binding. vulkan_core.h must precede openxr_platform.h so the
// XrGraphicsBindingVulkanKHR / XrSwapchainImageVulkanKHR structs see the Vk* types.
#include <vulkan/vulkan_core.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

export module RendererVK:OpenXRSession;

import Core;
import Core.glm;
import :VK;

// Minimal OpenXR session wrapper (Milestone 1: lifecycle + a cleared stereo image to the headset).
// Owns the XrInstance/system/session/reference-space and a single arraySize=2 swapchain that matches
// the multiview (2-layer) layout the rest of the renderer will grow into. Exposed as the Globals::openXR
// singleton: Renderer initializes it (only when VR is requested), and Instance/Device query it during
// their own creation for the runtime's required Vulkan extensions and physical device.
export class OpenXRSession final
{
public:
    OpenXRSession() {}
    ~OpenXRSession() { destroy(); }
    OpenXRSession(const OpenXRSession&) = delete;

    // Phase 1: create the XrInstance + system and query graphics requirements. Must run BEFORE the
    // Vulkan instance/device are created. Returns false if no runtime/HMD is available (caller should
    // then fall back to a desktop-only session).
    bool initInstanceAndSystem();

    // Vulkan instance/device creation inputs the runtime dictates. Valid after initInstanceAndSystem().
    std::vector<std::string> getRequiredVulkanInstanceExtensions() const;
    std::vector<std::string> getRequiredVulkanDeviceExtensions() const;
    vk::PhysicalDevice getVulkanGraphicsDevice(vk::Instance vkInstance) const;

    // Phase 2: create the XrSession bound to the (already created) Vulkan device, plus the reference
    // space and the stereo swapchain. Must run AFTER the Vulkan device exists.
    bool createSession(vk::Instance vkInstance, vk::PhysicalDevice vkPhysicalDevice, vk::Device vkDevice,
        uint32 graphicsQueueFamily, uint32 graphicsQueueIndex);

    // Pump the OpenXR event queue and advance the session state machine. Call once per frame.
    void pollEvents();

    // Per-frame: wait for the runtime's render cadence, begin the XR frame, and locate the head + eye
    // poses. Returns true if a frame was begun (the caller must then call endFrame to balance it).
    // No-op returning false when the session isn't running.
    bool beginFrame();
    // World-space head view matrix + position for the mono camera, given the play-space origin (driven
    // by keyboard locomotion). Valid after a successful beginFrame().
    void getHeadView(const glm::vec3& playSpaceOrigin, glm::mat4& outViewMatrix, glm::vec3& outPosition) const;
    // Blit the rendered frame into both eye swapchains and submit it to the compositor. Pass a null
    // source to submit an empty frame (keeps the begin/end pairing balanced when nothing was rendered).
    void endFrame(vk::Image source, vk::Extent2D sourceExtent, vk::ImageLayout sourceLayout);

    // True once the XR instance + system are up (i.e. VR is active). Instance/Device query this to
    // decide whether to pull in the runtime's required Vulkan extensions / physical device.
    bool isEnabled() const { return m_enabled; }
    bool isSessionRunning() const { return m_sessionRunning; }
    bool exitRequested() const { return m_exitRequested; }

    void destroy();

private:

    bool loadKhrFunctions();
    int64_t selectSwapchainFormat() const;

    static constexpr uint32 VIEW_COUNT = 2; // PRIMARY_STEREO

    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_space = XR_NULL_HANDLE;      // LOCAL reference space (world origin)
    XrSpace m_viewSpace = XR_NULL_HANDLE;  // VIEW reference space (head pose)

    // Per-frame state produced by beginFrame() and consumed by getHeadView()/endFrame().
    XrView m_views[VIEW_COUNT] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    XrPosef m_headPose{};
    bool m_headPoseValid = false;
    XrTime m_predictedDisplayTime = 0;
    bool m_frameActive = false;
    bool m_shouldRender = false;

    // One swapchain per eye (arraySize=1): the SteamVR Vulkan runtime does not support array
    // (arraySize>1) swapchains. Multiview rendering still happens into our own 2-layer array target;
    // each layer is copied into the matching eye swapchain before submit.
    XrSwapchain m_swapchains[VIEW_COUNT] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
    std::vector<XrSwapchainImageVulkanKHR> m_swapchainImages[VIEW_COUNT];
    XrViewConfigurationView m_viewConfig{ XR_TYPE_VIEW_CONFIGURATION_VIEW };
    int64_t m_swapchainFormat = 0;

    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
    bool m_enabled = false;       // XR instance + system created (VR active)
    bool m_sessionRunning = false;
    bool m_exitRequested = false;
    uint32 m_frameCounter = 0;

    // Cached Vulkan handles (owned elsewhere).
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkQueue m_vkQueue = VK_NULL_HANDLE;
    VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_clearCommandBuffer = VK_NULL_HANDLE; // M1 clear-frame scratch buffer
    uint32 m_graphicsQueueFamily = 0;

    PFN_xrGetVulkanInstanceExtensionsKHR m_pfnGetVulkanInstanceExtensions = nullptr;
    PFN_xrGetVulkanDeviceExtensionsKHR m_pfnGetVulkanDeviceExtensions = nullptr;
    PFN_xrGetVulkanGraphicsDeviceKHR m_pfnGetVulkanGraphicsDevice = nullptr;
    PFN_xrGetVulkanGraphicsRequirementsKHR m_pfnGetVulkanGraphicsRequirements = nullptr;
};

export namespace Globals
{
    // After the Vulkan device (XCU2) so its teardown (which uses the live VkDevice handle) runs before
    // the device is destroyed, and before/with the Renderer (XCU3) that drives it.
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    OpenXRSession openXR;
#pragma warning(default: 4075)
}
