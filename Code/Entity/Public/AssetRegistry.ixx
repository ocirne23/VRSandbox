export module Entity.AssetRegistry;

import Core;
import Entity.ObjectDescription;

export class AssetRegistry final
{
public:

    void scanDirectory(const std::string& rootDir = ".");

    void clear();

    const ObjectContainerDesc* findObjectContainer(const std::string& name) const;

    const std::string* findPrefab(const std::string& name) const;

    void addPrefab(const std::string& name, const std::string& path);

    const std::string* findRootForFile(const std::string& fileName) const;

    const std::unordered_map<std::string, ObjectContainerDesc>& getObjectContainers() const { return m_objectContainers; }

private:

    void registerFile(const std::string& path);

    std::unordered_map<std::string, ObjectContainerDesc> m_objectContainers;
    std::unordered_map<std::string, std::string> m_prefabs; // name -> .pre file path
    std::unordered_map<std::string, std::string> m_fileRoot; // lowercased file name -> root entity/prefab name
};

export namespace Globals
{
    AssetRegistry assetRegistry;
}
