module;

#include <vector>
#include <VK.h>

export module RendererVK.Instance;

export class Window;

export class Instance
{
public:
	Instance();
	~Instance();
	Instance(const Instance&) = delete;

	bool initialize(Window& window, bool enableValidationLayers);
	void setBreakOnValidationLayerError(bool value) { m_breakOnValidationLayerError = value; }

	vk::Instance getHandle() const { return m_instance; }

	const std::vector<const char*>& getEnabledLayers() const { return m_enabledLayers; }

	bool supportsLayer(const char* pLayerName) const;
	bool supportsExtension(const char* pExtensionName) const;

private:

	vk::Instance m_instance;
	bool m_breakOnValidationLayerError = false;
	std::vector<const char*> m_enabledLayers;

	std::vector<vk::ExtensionProperties> m_supportedExtensions;
	std::vector<vk::LayerProperties> m_supportedLayers;
};