export module File.MaterialData;

import Core;
import Core.glm;

export struct aiMaterial;
export enum aiTextureType : int;

export class MaterialData final
{
public:
    using TextureType = aiTextureType;

    MaterialData();
    ~MaterialData();
    MaterialData(const MaterialData&) = delete;
    MaterialData(MaterialData&&) = default;

    bool initialize(const aiMaterial* pMaterial);

    const char* getName() const;
    
    glm::vec3 getBaseColor() const;
    glm::vec3 getEmissiveColor() const;
    glm::vec3 getSpecularColor() const;
    float getRoughnessFactor() const;
    float getMetalnessFactor() const;
    float getOpacity() const;
    float getEmissiveIntensity() const;
    float getRefractiveIndex() const;

    std::string getTexturePath(TextureType type) const;

private:

    const aiMaterial* m_pMaterial = nullptr;
};