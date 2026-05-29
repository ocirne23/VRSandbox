module;

#include <assimp/material.h>
#include <assimp/GltfMaterial.h>
#include <cstring>

module File.MaterialData;

import File.Assimp;

using namespace Assimp;

MaterialData::MaterialData()
{
}

MaterialData::~MaterialData()
{
}

bool MaterialData::initialize(const aiMaterial* pMaterial)
{
    m_pMaterial = pMaterial;
    return true;
}

uint32 MaterialData::getDiffuseTexIdx() const
{
    aiString path;
    aiReturn ret = aiGetMaterialString(m_pMaterial, AI_MATKEY_TEXTURE_DIFFUSE(0), &path);
    if (ret != aiReturn_SUCCESS)
    {
        return UINT32_MAX;
    }
    if (path.length < 2)
    {
        assert(false && "Failed to find diffuse texture path");
        return UINT32_MAX;
    }
    const bool isEmbedded = path.C_Str()[0] == '*';
    uint32 diffuseTexIdx = UINT32_MAX;
    if (isEmbedded)
    {
        diffuseTexIdx = atoi(path.C_Str() + 1);
    }
    assert(isEmbedded && "Implement non embedded textures");
    return diffuseTexIdx;
}

uint32 MaterialData::getNormalTexIdx() const
{
    aiString path;
    aiReturn ret = aiGetMaterialString(m_pMaterial, AI_MATKEY_TEXTURE_NORMALS(0), &path);
    if (ret != aiReturn_SUCCESS)
    {
        return UINT32_MAX;
    }
    if (path.length < 2)
    {
        assert(false && "Failed to find normal texture path");
        return UINT32_MAX;
    }
    const bool isEmbedded = path.C_Str()[0] == '*';
    uint32 normalTexIdx = UINT32_MAX;
    if (isEmbedded)
    {
        normalTexIdx = atoi(path.C_Str() + 1);
    }
    assert(isEmbedded && "Implement non embedded textures");
    return normalTexIdx;
}

uint32 MaterialData::getOpacityTexIdx() const
{
    aiString path;
    aiReturn ret = aiGetMaterialString(m_pMaterial, AI_MATKEY_TEXTURE_OPACITY(0), &path);
    if (ret != aiReturn_SUCCESS)
    {
        return UINT32_MAX;
    }
    if (path.length < 2)
    {
        assert(false && "Failed to find opacity texture path");
        return UINT32_MAX;
    }
    const bool isEmbedded = path.C_Str()[0] == '*';
    uint32 opacityTexIdx = UINT32_MAX;
    if (isEmbedded)
    {
        opacityTexIdx = atoi(path.C_Str() + 1);
    }
    assert(isEmbedded && "Implement non embedded textures");
    return opacityTexIdx;
}

const char* MaterialData::getName() const
{
    static thread_local aiString name;
    m_pMaterial->Get(AI_MATKEY_NAME, name);
    return name.C_Str();
}

glm::vec3 MaterialData::getBaseColor() const
{
    aiColor4D color;
    aiReturn res = aiGetMaterialColor(m_pMaterial, AI_MATKEY_BASE_COLOR, &color);
    if (res == aiReturn_SUCCESS)
        return glm::vec3(color.r, color.g, color.b);
    else
    {
        aiReturn res2 = aiGetMaterialColor(m_pMaterial, AI_MATKEY_COLOR_DIFFUSE, &color);
        if (res2 == aiReturn_SUCCESS)
            return glm::vec3(color.r, color.g, color.b);
        else
            return glm::vec3(1);
    }
}

glm::vec3 MaterialData::getEmissiveColor() const
{
    aiColor4D color;
    aiReturn res = aiGetMaterialColor(m_pMaterial, AI_MATKEY_COLOR_EMISSIVE, &color);
    if (res == aiReturn_SUCCESS)
        return glm::vec3(color.r, color.g, color.b);
    else
        return glm::vec3(0);
}

glm::vec3 MaterialData::getSpecularColor() const
{
    aiColor4D color;
    aiReturn res = aiGetMaterialColor(m_pMaterial, AI_MATKEY_COLOR_SPECULAR, &color);
    if (res == aiReturn_SUCCESS)
        return glm::vec3(color.r, color.g, color.b);
    else
        return glm::vec3(1);
}

float MaterialData::getRoughnessFactor() const
{
    float f;
    aiReturn res = aiGetMaterialFloat(m_pMaterial, AI_MATKEY_ROUGHNESS_FACTOR, &f);
    if (res == aiReturn_SUCCESS)
        return f;
    else
        return 0.0f;
}

float MaterialData::getMetalnessFactor() const
{
    float f;
    aiReturn res = aiGetMaterialFloat(m_pMaterial, AI_MATKEY_METALLIC_FACTOR, &f);
    if (res == aiReturn_SUCCESS)
        return f;
    else
        return 0.0f;
}

float MaterialData::getOpacity() const
{
    float f;
    aiReturn res = aiGetMaterialFloat(m_pMaterial, AI_MATKEY_OPACITY, &f);
    if (res == aiReturn_SUCCESS)
        return f;
    else
        return 1.0f;
}

MaterialData::AlphaMode MaterialData::getAlphaMode() const
{
    // glTF stores the alpha mode explicitly as "OPAQUE" / "MASK" / "BLEND".
    aiString mode;
    aiReturn res = aiGetMaterialString(m_pMaterial, AI_MATKEY_GLTF_ALPHAMODE, &mode);
    if (res == aiReturn_SUCCESS)
    {
        if (strcmp(mode.C_Str(), "BLEND") == 0)
            return AlphaMode::Blend;
        if (strcmp(mode.C_Str(), "MASK") == 0)
            return AlphaMode::Mask;
        return AlphaMode::Opaque;
    }
    // Non-glTF assets don't expose an alpha mode; fall back to inferring blend from opacity.
    return getOpacity() < 1.0f ? AlphaMode::Blend : AlphaMode::Opaque;
}

float MaterialData::getAlphaCutoff() const
{
    float f;
    aiReturn res = aiGetMaterialFloat(m_pMaterial, AI_MATKEY_GLTF_ALPHACUTOFF, &f);
    if (res == aiReturn_SUCCESS)
        return f;
    else
        return 0.5f; // glTF default
}

float MaterialData::getEmissiveIntensity() const
{
    float f;
    aiReturn res = aiGetMaterialFloat(m_pMaterial, AI_MATKEY_EMISSIVE_INTENSITY, &f);
    if (res == aiReturn_SUCCESS)
        return f;
    else
        return 1.0f;
}

float MaterialData::getRefractiveIndex() const
{
    float f;
    aiReturn res = aiGetMaterialFloat(m_pMaterial, AI_MATKEY_REFRACTI, &f);
    if (res == aiReturn_SUCCESS)
        return f;
    else
        return 0.04f;
}

std::string MaterialData::getTexturePath(TextureType type) const
{
    assert(m_pMaterial->GetTextureCount(type) <= 1 && "Only one texture per type supported");

    aiString path;
    m_pMaterial->GetTexture(type, 0, &path);
    if (!path.length)
        return "";
    else
        return std::filesystem::path(path.C_Str()).lexically_proximate(std::filesystem::current_path()).string();
}