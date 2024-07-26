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
    m_pName = m_pMaterial->GetName().C_Str();

    aiString diffuseTexturePath;
    m_pMaterial->GetTexture(aiTextureType_DIFFUSE, 0, &diffuseTexturePath);
    if (!diffuseTexturePath.length)
        m_diffuseTexturePath = "Textures/fallback.png";
    else
        m_diffuseTexturePath = diffuseTexturePath.C_Str();

    return true;
}