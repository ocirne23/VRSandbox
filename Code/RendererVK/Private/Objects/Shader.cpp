module RendererVK:Shader;

import Core;
import File.FileSystem;
import :VK;
import :Device;
import :glslang;

Shader::Shader() {}
Shader::~Shader()
{
    if (m_shaderModule)
        Globals::device.getDevice().destroyShaderModule(m_shaderModule);
}

bool Shader::initializeFromFile(vk::ShaderStageFlagBits stage, const std::string& filePath, const std::vector<ShaderDefine>& defines, bool assertOnFailure)
{
    const std::string fileContent = FileSystem::readFileStr(filePath);
    if (fileContent.empty())
    {
        assert((!assertOnFailure) && "Failed to open shader file");
        return false;
    }

    return initialize(stage, fileContent, filePath, defines, assertOnFailure);
}

bool Shader::initialize(vk::ShaderStageFlagBits stage, const std::string& shaderStr, const std::string& debugFilePath, const std::vector<ShaderDefine>& defines, bool assertOnFailure)
{
    std::vector<unsigned int> spirv;
    if (!GLSLtoSPV(stage, shaderStr, spirv, debugFilePath, defines))
    {
        assert((!assertOnFailure) && "Failed to compile shader SPIRV");
        return false;
    }

    vk::ShaderModuleCreateInfo createInfo;
    createInfo.codeSize = spirv.size() * sizeof(unsigned int);
    createInfo.pCode = spirv.data();

    auto createResult = Globals::device.getDevice().createShaderModule(createInfo);
    if (createResult.result != vk::Result::eSuccess)
    {
        assert((!assertOnFailure) && "Failed to create shader module");
        return false;
    }
    m_shaderModule = createResult.value;

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

class ShaderIncluder final : public glslang::TShader::Includer
{
public:
    explicit ShaderIncluder(const std::string& rootFilePath)
    {
        m_rootDir = std::filesystem::path(rootFilePath).parent_path();
    }

    IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t /*depth*/) override
    {
        return resolve(headerName, includerName);
    }

    IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t /*depth*/) override
    {
        return resolve(headerName, includerName);
    }

    void releaseInclude(IncludeResult* /*result*/) override {}

private:
    IncludeResult* resolve(const char* headerName, const char* includerName)
    {
        std::vector<std::filesystem::path> candidates;
        if (includerName != nullptr && includerName[0] != '\0')
        {
            const std::filesystem::path includerDir = std::filesystem::path(includerName).parent_path();
            if (!includerDir.empty())
                candidates.push_back(includerDir / headerName);
        }
        candidates.push_back(m_rootDir / headerName);

        for (const std::filesystem::path& candidate : candidates)
        {
            const std::string resolvedPath = candidate.lexically_normal().generic_string();
            std::string content = FileSystem::readFileStr(resolvedPath);
            if (content.empty())
                continue;

            const std::string& stored = *m_contents.emplace_back(std::make_unique<std::string>(std::move(content)));
            m_results.push_back(std::make_unique<IncludeResult>(resolvedPath, stored.data(), stored.size(), nullptr));
            return m_results.back().get();
        }
        return nullptr;
    }

    std::filesystem::path m_rootDir;
    std::vector<std::unique_ptr<std::string>> m_contents;
    std::vector<std::unique_ptr<IncludeResult>> m_results;
};

static std::string buildPreamble(const std::vector<ShaderDefine>& defines)
{
    std::string preamble = "#extension GL_ARB_shading_language_include : require\n";
    for (const ShaderDefine& define : defines)
    {
        preamble += "#define " + define.name;
        if (!define.value.empty())
            preamble += " " + define.value;
        preamble += "\n";
    }
    return preamble;
}

bool Shader::GLSLtoSPV(const vk::ShaderStageFlagBits type, const std::string& source, std::vector<unsigned int>& spirv, const std::string& debugFilePath, const std::vector<ShaderDefine>& defines)
{
    EShLanguage stage = translateShaderStage(type);
    glslang::TShader shader(stage);

    const char* shaderStrings[1] = { source.data() };
    shader.setStrings(shaderStrings, 1);
    shader.addSourceText(source.c_str(), source.length());
    shader.setSourceFile(debugFilePath.c_str());
    shader.setDebugInfo(true);

    // Target Vulkan 1.3 / SPIR-V 1.6 so modern extensions compile (notably GL_EXT_ray_query, which
    // needs the RayQueryKHR capability and SPIR-V 1.4+). Without this glslang defaults to SPIR-V 1.0.
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

    const std::string preamble = buildPreamble(defines);
    if (!preamble.empty())
        shader.setPreamble(preamble.c_str());

    ShaderIncluder includer(debugFilePath);

    // Enable SPIR-V and Vulkan rules when parsing GLSL
    EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
    if (!shader.parse(GetDefaultResources(), 100, ENoProfile, false, false, messages, includer))
    {
        puts(shader.getInfoLog());
        puts(shader.getInfoDebugLog());
        std::cout.flush();
        return false;  // something didn't work
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages))
    {
        puts(program.getInfoLog());
        puts(program.getInfoDebugLog());
        std::cout.flush();
        return false;
    }

    glslang::SpvOptions spvOptions;
    spvOptions.generateDebugInfo = false;
    spvOptions.stripDebugInfo = true;
    spvOptions.disableOptimizer = false;
    spvOptions.optimizeSize = false;
    spvOptions.disassemble = false;
    spvOptions.validate = true;
    spvOptions.emitNonSemanticShaderDebugInfo = false;
    spvOptions.emitNonSemanticShaderDebugSource = false;
    spvOptions.compileOnly = false;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv, &spvOptions);
    return true;
}