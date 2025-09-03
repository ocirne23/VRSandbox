module RendererVK.Instance;

import Core;
import Core.SDL;
import Core.Window;

import RendererVK.VK;
import RendererVK.Device;

#ifdef USE_AFTERMATH
import RendererVK.Aftermath;
import Core.Windows;
#endif

#ifdef USE_AFTERMATH
void ShaderDebugInfoCallback(const void* pShaderDebugInfo, const uint32_t shaderDebugInfoSize, void* pUserData)
{
    printf("ShaderDebugInfoCallback\n");
}

void CrashDumpDescriptionCallback(PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription addDescription, void* pUserData)
{
    printf("CrashDumpDescriptionCallback\n");
}

void ResolveMarkerCallback(const void* pMarkerData, const uint32_t markerDataSize, void* pUserData, void** ppResolvedMarkerData, uint32_t* pResolvedMarkerDataSize)
{
    printf("ResolveMarkerCallback\n");
}


inline std::string  AftermathErrorMessage(GFSDK_Aftermath_Result result)
{
    switch (result)
    {
    case GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported:
        return "Unsupported driver version - requires an NVIDIA R495 display driver or newer.";
    default:
        return "Aftermath Error " + std::to_string((int)result);
    }
}

#ifdef _WIN32
#define AFTERMATH_CHECK_ERROR(FC)                                                                       \
[&]() {                                                                                                 \
    GFSDK_Aftermath_Result _result = FC;                                                                \
    if (!GFSDK_Aftermath_SUCCEED(_result))                                                              \
    {                                                                                                   \
        MessageBoxA(0, AftermathErrorMessage(_result).c_str(), "Aftermath Error", MB_OK);               \
        exit(1);                                                                                        \
    }                                                                                                   \
}()
#else
#define AFTERMATH_CHECK_ERROR(FC)                                                                       \
[&]() {                                                                                                 \
    GFSDK_Aftermath_Result _result = FC;                                                                \
    if (!GFSDK_Aftermath_SUCCEED(_result))                                                              \
    {                                                                                                   \
        printf("%s\n", AftermathErrorMessage(_result).c_str());                                         \
        fflush(stdout);                                                                                 \
        exit(1);                                                                                        \
    }                                                                                                   \
}()
#endif

void GpuCrashDumpCallback(const void* pGpuCrashDump, const uint32_t gpuCrashDumpSize, void* pUserData)
{
    printf("GpuCrashDumpCallback\n");

    GFSDK_Aftermath_GpuCrashDump_Decoder decoder = {};
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_CreateDecoder(
        GFSDK_Aftermath_Version_API,
        pGpuCrashDump,
        gpuCrashDumpSize,
        &decoder));

    // Use the decoder object to read basic information, like application
    // name, PID, etc. from the GPU crash dump.
    GFSDK_Aftermath_GpuCrashDump_BaseInfo baseInfo = {};
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetBaseInfo(decoder, &baseInfo));

    // Use the decoder object to query the application name that was set
    // in the GPU crash dump description.
    uint32_t applicationNameLength = 0;
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetDescriptionSize(
        decoder,
        GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
        &applicationNameLength));

    std::vector<char> applicationName(applicationNameLength);

    std::string appName = "App.exe";

    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetDescription(
        decoder,
        GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
        uint32_t(appName.size()),
        appName.data()));

    // Create a unique file name for writing the crash dump data to a file.
    // Note: due to an Nsight Aftermath bug (will be fixed in an upcoming
    // driver release) we may see redundant crash dumps. As a workaround,
    // attach a unique count to each generated file name.
    static int count = 0;
    const std::string baseFileName =
        std::string(appName)
        + "-"
        + std::to_string(baseInfo.pid)
        + "-"
        + std::to_string(++count);

    // Write the crash dump data to a file using the .nv-gpudmp extension
    // registered with Nsight Graphics.
    const std::string crashDumpFileName = baseFileName + ".nv-gpudmp";
    std::string path = std::filesystem::current_path().string();
    path += "\\" + crashDumpFileName;
    std::ofstream dumpFile(path, std::ios::out | std::ios::binary);
    if (dumpFile)
    {
        dumpFile.write((const char*)pGpuCrashDump, gpuCrashDumpSize);
        dumpFile.close();
    }

    // Decode the crash dump to a JSON string.
    // Step 1: Generate the JSON and get the size.
    uint32_t jsonSize = 0;
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GenerateJSON(
        decoder,
        GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO,
        GFSDK_Aftermath_GpuCrashDumpFormatterFlags_NONE,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &jsonSize));
    // Step 2: Allocate a buffer and fetch the generated JSON.
    std::vector<char> json(jsonSize);
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_GetJSON(
        decoder,
        uint32_t(json.size()),
        json.data()));

    // Write the crash dump data as JSON to a file.
    const std::string jsonFileName = crashDumpFileName + ".json";
    std::ofstream jsonFile(jsonFileName, std::ios::out | std::ios::binary);
    if (jsonFile)
    {
        // Write the JSON to the file (excluding string termination)
        jsonFile.write(json.data(), json.size() - 1);
        jsonFile.close();
    }

    // Destroy the GPU crash dump decoder object.
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_GpuCrashDump_DestroyDecoder(decoder));
}
#endif
Instance::Instance() {}
Instance::~Instance()
{
    Globals::device.destroy();
    destroy();
}

static vk::Bool32 debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT       messageSeverity,
    vk::DebugUtilsMessageTypeFlagsEXT              messageTypes,
    const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    const char* severity = "UNKNOWN";
    
    if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
        severity = "ERROR";
    else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
        severity = "WARNING";
    else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo)
        severity = "INFO";
    else if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
        severity = "VERBOSE";
    printf("Validation layer %s: %s\n", severity, pCallbackData->pMessage);
    bool* pBreakOnValidationLayerError = reinterpret_cast<bool*>(pUserData);
    if (*pBreakOnValidationLayerError && (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eError))
    {
        __debugbreak();
    }
    return vk::False;
}

bool Instance::initialize(Window& window, bool enableValidationLayers)
{
#ifdef USE_AFTERMATH
    AFTERMATH_CHECK_ERROR(GFSDK_Aftermath_EnableGpuCrashDumps(
        GFSDK_Aftermath_Version_API,
        GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
        GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks, // Let the Nsight Aftermath library cache shader debug information.
        GpuCrashDumpCallback,                                             // Register callback for GPU crash dumps.
        ShaderDebugInfoCallback,                                          // Register callback for shader debug information.
        CrashDumpDescriptionCallback,                                     // Register callback for GPU crash dump description.
        ResolveMarkerCallback,                                            // Register callback for resolving application-managed markers.
        nullptr));                                                        // Set the GpuCrashTracker object as user data for the above callbacks.
#endif

    m_supportedExtensions = vk::enumerateInstanceExtensionProperties();
    m_supportedLayers = vk::enumerateInstanceLayerProperties();
    m_apiVersion = VK_MAKE_API_VERSION(0, 1, 3, 0);

    uint32 numExtensions = 0;
    const char* const* ppExtensions = SDL_Vulkan_GetInstanceExtensions(&numExtensions);
    std::vector<const char*> extensions;

    for (uint32 i = 0; i < numExtensions; i++)
        if (ppExtensions[i] && supportsExtension(ppExtensions[i]))
            extensions.push_back(ppExtensions[i]);

    vk::ApplicationInfo appInfo{ .pApplicationName = "App", .applicationVersion = VK_MAKE_VERSION(1, 0, 0), .pEngineName = "VRSandbox", .engineVersion = VK_MAKE_VERSION(1, 0, 0), .apiVersion = m_apiVersion };
    vk::InstanceCreateInfo createInfo{ .pApplicationInfo = &appInfo };

    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debugCallback,
        .pUserData = &m_breakOnValidationLayerError,
    };

    if (enableValidationLayers)
    {
        if (supportsLayer(VK_VALIDATION_LAYER_NAME))
            m_enabledLayers.push_back(VK_VALIDATION_LAYER_NAME);
        if (supportsExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (supportsExtension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
            extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        createInfo.pNext = &debugCreateInfo;
    }
    createInfo.enabledLayerCount = (uint32)m_enabledLayers.size();
    createInfo.ppEnabledLayerNames = m_enabledLayers.data();

    createInfo.enabledExtensionCount = static_cast<uint32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    m_instance = vk::createInstance(createInfo);
    if (!m_instance)
    {
        assert(false && "Failed to create instance");
        return false;
    }

    if (enableValidationLayers)
    {
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo2 = debugCreateInfo;
        VkDebugUtilsMessengerEXT debugMessenger;
        auto createMessengerFunc = (PFN_vkCreateDebugUtilsMessengerEXT)m_instance.getProcAddr("vkCreateDebugUtilsMessengerEXT");
        createMessengerFunc(m_instance, &debugCreateInfo2, nullptr, &debugMessenger);
        m_debugMessenger = debugMessenger;
    }

    return true;
}

void Instance::destroy()
{
    if (m_instance)
    {
        auto destroyMessengerFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)m_instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT");
        destroyMessengerFunc(m_instance, m_debugMessenger, nullptr);
        m_instance.destroy();
    }
    m_instance = nullptr;
}

bool Instance::supportsExtension(const char* pExtensionName) const
{
    for (auto extension : m_supportedExtensions)
        if (strcmp(pExtensionName, extension.extensionName) == 0)
            return true;
    return false;
}

bool Instance::supportsLayer(const char* pLayerName) const
{
    for (auto layer : m_supportedLayers)
        if (strcmp(pLayerName, layer.layerName) == 0)
            return true;
    return false;
}