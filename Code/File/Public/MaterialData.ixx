export module File.MaterialData;

import Core;

export struct aiMaterial;

export class MaterialData final
{
public:
    MaterialData();
    ~MaterialData();
    MaterialData(const MaterialData&) = delete;
    MaterialData(MaterialData&&) = default;

    bool initialize(const aiMaterial* pMaterial);

private:

    const char* m_pName = nullptr;
    const aiMaterial* m_pMaterial = nullptr;
    std::string m_diffuseTexturePath;
};