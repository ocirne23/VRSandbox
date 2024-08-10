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
    aiString name;
    m_pMaterial->Get(AI_MATKEY_NAME, name);
    return name.C_Str();
}

const glm::vec3 MaterialData::getDiffuseColor() const
{
    aiColor3D color;
    m_pMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, color);
    return glm::vec3(color.r, color.g, color.b);
}

const glm::vec3 MaterialData::getSpecularColor() const
{
    aiColor3D color;
    m_pMaterial->Get(AI_MATKEY_COLOR_SPECULAR, color);
    return glm::vec3(color.r, color.g, color.b);
}

const glm::vec3 MaterialData::getAmbientColor() const
{
    aiColor3D color;
    m_pMaterial->Get(AI_MATKEY_COLOR_AMBIENT, color);
    return glm::vec3(color.r, color.g, color.b);
}

const glm::vec3 MaterialData::getEmissiveColor() const
{
    aiColor3D color;
    m_pMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, color);
    return glm::vec3(color.r, color.g, color.b);
}

const glm::vec3 MaterialData::getTransparentColor() const
{
    aiColor3D color;
    m_pMaterial->Get(AI_MATKEY_COLOR_TRANSPARENT, color);
    return glm::vec3(color.r, color.g, color.b);
}

const glm::vec3 MaterialData::getReflectiveColor() const
{
    aiColor3D color;
    m_pMaterial->Get(AI_MATKEY_COLOR_REFLECTIVE, color);
    return glm::vec3(color.r, color.g, color.b);
}

float MaterialData::getOpacity() const
{
    float opacity;
    m_pMaterial->Get(AI_MATKEY_OPACITY, opacity);
    return opacity;
}

float MaterialData::getShininess() const
{
    float shininess;
    m_pMaterial->Get(AI_MATKEY_SHININESS, shininess);
    return shininess;
}

float MaterialData::getShininessStrength() const
{
    float shininessStrength;
    m_pMaterial->Get(AI_MATKEY_SHININESS_STRENGTH, shininessStrength);
    return shininessStrength;
}

float MaterialData::getRefractiveIndex() const
{
    float refractiveIndex;
    m_pMaterial->Get(AI_MATKEY_REFRACTI, refractiveIndex);
    return refractiveIndex;
}

const std::string MaterialData::getTexturePath(TextureType type) const
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