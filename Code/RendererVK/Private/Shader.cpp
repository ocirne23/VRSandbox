module RendererVK.Shader;

import Core;
import RendererVK.VK;
import RendererVK.Device;
import RendererVK.glslang;

Shader::Shader() {}
Shader::~Shader()
{
    if (m_shaderModule)
        VK::g_dev.getDevice().destroyShaderModule(m_shaderModule);
}

bool Shader::initializeFromFile(vk::ShaderStageFlagBits stage, const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        assert(false && "Failed to open shader file");
        return false;
    }

    size_t fileSize = (size_t)file.tellg();
    std::string fileStr(fileSize, ' ');
    file.seekg(0);
    file.read(fileStr.data(), fileSize);
    file.close();

    return initialize(stage, fileStr);
}

bool Shader::initialize(vk::ShaderStageFlagBits stage, const std::string& shaderStr)
{
    std::vector<unsigned int> spirv;
    if (!GLSLtoSPV(stage, shaderStr, spirv))
    {
        assert(false && "Failed to compile shader SPIRV");
        return false;
    }

    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = spirv.size() * sizeof(unsigned int);
    createInfo.pCode = spirv.data();

    m_shaderModule = VK::g_dev.getDevice().createShaderModule(createInfo);
    if (!m_shaderModule)
    {
        assert(false && "Failed to create shader module");
        return false;
    }

    return true;
}

EShLanguage translateShaderStage(vk::ShaderStageFlagBits stage)
{
    switch (stage)
    {
    case vk::ShaderStageFlagBits::eVertex: return EShLangVertex;
    case vk::ShaderStageFlagBits::eTessellationControl: return EShLangTessControl;
    case vk::ShaderStageFlagBits::eTessellationEvaluation: return EShLangTessEvaluation;
    case vk::ShaderStageFlagBits::eGeometry: return EShLangGeometry;
    case vk::ShaderStageFlagBits::eFragment: return EShLangFragment;
    case vk::ShaderStageFlagBits::eCompute: return EShLangCompute;
    case vk::ShaderStageFlagBits::eRaygenNV: return EShLangRayGenNV;
    case vk::ShaderStageFlagBits::eAnyHitNV: return EShLangAnyHitNV;
    case vk::ShaderStageFlagBits::eClosestHitNV: return EShLangClosestHitNV;
    case vk::ShaderStageFlagBits::eMissNV: return EShLangMissNV;
    case vk::ShaderStageFlagBits::eIntersectionNV: return EShLangIntersectNV;
    case vk::ShaderStageFlagBits::eCallableNV: return EShLangCallableNV;
    case vk::ShaderStageFlagBits::eTaskNV: return EShLangTaskNV;
    case vk::ShaderStageFlagBits::eMeshNV: return EShLangMeshNV;
    default: assert(false && "Unknown shader stage"); return EShLangVertex;
    }
}

bool Shader::GLSLtoSPV(const vk::ShaderStageFlagBits type, const std::string& source, std::vector<unsigned int>& spirv)
{
    EShLanguage stage = translateShaderStage(type);
    glslang::TShader shader(stage);

    const char* shaderStrings[1] = { source.data() };
    shader.setStrings(shaderStrings, 1);

    // Enable SPIR-V and Vulkan rules when parsing GLSL
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 100, false, messages))
    {
        puts(shader.getInfoLog());
        puts(shader.getInfoDebugLog());
        return false;  // something didn't work
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        puts(shader.getInfoLog());
        puts(shader.getInfoDebugLog());
        std::cout.flush();
        return false;
    }

    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    return true;
}