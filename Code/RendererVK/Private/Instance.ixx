export module RendererVK.Instance;

import Core;
import RendererVK.VK;

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

private:

	vk::Instance m_instance;
	bool m_breakOnValidationLayerError = false;
	std::vector<const char*> m_enabledLayers;

	std::vector<vk::ExtensionProperties> m_supportedExtensions;
	std::vector<vk::LayerProperties> m_supportedLayers;
	VkDebugUtilsMessengerEXT m_debugMessenger;
};

export namespace VK
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU1")
	Instance g_inst;
#pragma warning(default: 4075)
}