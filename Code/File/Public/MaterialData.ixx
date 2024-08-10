export module File.MaterialData;

import Core;
import Core.glm;

export struct aiMaterial;
export enum aiTextureType;

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
    const glm::vec3 getDiffuseColor() const;
    const glm::vec3 getSpecularColor() const;
    const glm::vec3 getAmbientColor() const;
    const glm::vec3 getEmissiveColor() const;
    const glm::vec3 getTransparentColor() const;
    const glm::vec3 getReflectiveColor() const;
    float getOpacity() const;
    float getShininess() const;
    float getShininessStrength() const;
    float getRefractiveIndex() const;
    const std::string getTexturePath(TextureType type) const;

private:

    const aiMaterial* m_pMaterial = nullptr;
};