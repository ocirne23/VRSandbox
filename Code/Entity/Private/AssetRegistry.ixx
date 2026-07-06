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

    const AnimationClipDesc* findClip(const std::string& name) const;

    const AnimatorDesc* findAnimator(const std::string& name) const;

    const std::string* findPrefab(const std::string& name) const;

    void addPrefab(const std::string& name, const std::string& path);

    const std::string* findRootForFile(const std::string& fileName) const;

    const std::unordered_map<std::string, ObjectContainerDesc>& getObjectContainers() const { return m_objectContainers; }

    // .apl Animator entries by name — for editor tooling (Entity Editor) to offer a searchable list.
    const std::unordered_map<std::string, AnimatorDesc>& getAnimators() const { return m_animators; }

private:

    void registerFile(const std::string& path);

    std::unordered_map<std::string, ObjectContainerDesc> m_objectContainers;
    std::unordered_map<std::string, AnimationClipDesc> m_clips;  // .anm Animation entries, global by name
    std::unordered_map<std::string, AnimatorDesc> m_animators;   // .apl Animator entries, global by name
    std::unordered_map<std::string, std::string> m_prefabs; // name -> .pre file path
    std::unordered_map<std::string, std::string> m_fileRoot; // lowercased file name -> root entity/prefab name
};

export namespace Globals
{
    AssetRegistry assetRegistry;
}
