export module Entity:AssetRegistry;

import Core;
import :ObjectDescription;
import :AnimationDescription;

export class AssetRegistry final
{
public:

    void scanDirectory(const std::string& rootDir = ".");

    void clear();

    const ObjectContainerDesc* findObjectContainer(const std::string& name) const;

    const SpawnableDesc* findSpawnable(const std::string& name) const;

    const AnimationClipDesc* findClip(const std::string& name) const;

    const AnimatorDesc* findAnimator(const std::string& name) const;

    const std::string* findPrefab(const std::string& name) const;

    void addPrefab(const std::string& name, const std::string& path);

    const std::string* findRootForFile(const std::string& fileName) const;

    const std::unordered_map<std::string, ObjectContainerDesc>& getObjectContainers() const { return m_objectContainers; }

private:

    void registerFile(const std::string& path);

    std::unordered_map<std::string, ObjectContainerDesc> m_objectContainers;
    std::unordered_map<std::string, SpawnableDesc> m_spawnables; // StaticMesh/SkinnedMesh entries, global by name
    std::unordered_map<std::string, AnimationClipDesc> m_clips;  // .anm Animation entries, global by name
    std::unordered_map<std::string, AnimatorDesc> m_animators;   // .apl Animator entries, global by name
    std::unordered_map<std::string, std::string> m_prefabs; // name -> .pre file path
    std::unordered_map<std::string, std::string> m_fileRoot; // lowercased file name -> root entity/prefab name
};

export namespace Globals
{
    AssetRegistry assetRegistry;
}
