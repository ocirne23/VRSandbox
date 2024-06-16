module;

#include <string>
#include <vector>
#include "VK.h"

export module RendererVK.Shader;

import RendererVK.Device;

export class Shader
{
public:
	Shader();
	~Shader();
	Shader(const Shader&) = delete;

	bool initializeFromFile(const Device& device, vk::ShaderStageFlagBits stage, const std::string& filePath);
	bool initialize(const Device& device, vk::ShaderStageFlagBits stage, const std::string& shaderStr);

	static bool GLSLtoSPV(const vk::ShaderStageFlagBits type, const std::string& source, std::vector<unsigned int>& spirv);

	vk::ShaderModule getModule() const { return m_shaderModule; }

private:

	vk::ShaderModule m_shaderModule;
};