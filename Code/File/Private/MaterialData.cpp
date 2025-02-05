module;

#include <assimp/material.h>

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
    const uint32 count = m_pMaterial->GetTextureCount(type);
    assert(count <= 1 && "Only one texture per type supported");

    aiString path;
    m_pMaterial->GetTexture(type, 0, &path);
    if (!path.length)
        return "";
    else
        return std::filesystem::path(path.C_Str()).lexically_proximate(std::filesystem::current_path()).string();
}