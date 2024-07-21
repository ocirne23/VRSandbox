export module RendererVK.Shader;

import Core;
import RendererVK.VK;

export class Shader final
{
public:
	Shader();
	~Shader();
	Shader(const Shader&) = delete;

	bool initializeFromFile(vk::ShaderStageFlagBits stage, const std::string& filePath);
	bool initialize(vk::ShaderStageFlagBits stage, const std::string& shaderStr);

	static bool GLSLtoSPV(const vk::ShaderStageFlagBits type, const std::string& source, std::vector<unsigned int>& spirv);

	vk::ShaderModule getModule() const { return m_shaderModule; }

private:

	vk::ShaderModule m_shaderModule;
};