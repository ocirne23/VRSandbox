export module RendererVK:Instance;

import Core;
import :VK;

export class Window;

export class Instance final
{
public:
    Instance();
    ~Instance();
    Instance(const Instance&) = delete;

    bool initialize(Window& window, bool enableValidationLayers);
    void destroy();

    void setBreakOnValidationLayerError(bool value) { m_breakOnValidationLayerError = value; }
    vk::Instance getInstance() const { return m_instance; }
    const std::vector<const char*>& getEnabledLayers() const { return m_enabledLayers; }
    bool supportsLayer(const char* pLayerName) const;
    bool supportsExtension(const char* pExtensionName) const;
    uint32 getApiVersion() const { return m_apiVersion; }

private:

    vk::Instance m_instance;
    uint32 m_apiVersion = 0;
    bool m_breakOnValidationLayerError = false;
    std::vector<const char*> m_enabledLayers;

    std::vector<vk::ExtensionProperties> m_supportedExtensions;
    std::vector<vk::LayerProperties> m_supportedLayers;
    vk::DebugUtilsMessengerEXT m_debugMessenger;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU1")
    Instance instance;
#pragma warning(default: 4075)
}