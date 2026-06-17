export module Entity.AssetRegistry;

import Core;
import Entity.ObjectDescription;


// Scans the asset tree for asset files (.oc / .ent / .pre), parses every top-level declaration, and
// registers it by the name that follows its keyword (e.g. "ObjectContainer sponza" -> "sponza").
// Names form a flat reference space per kind: a declaration's name is what other declarations
// (e.g. an Entity's RenderNode component) refer to. File names need not match declaration names.
// A spawnable file (.ent / .pre) declares exactly one root entity/prefab; an .oc file may declare
// any number of ObjectContainers (asset definitions, not spawnable roots).
export class AssetRegistry final
{
public:

    // Recursively scans rootDir (defaults to the Assets working dir) and registers everything found.
    // Re-scanning clears previous registrations first.
    void scanDirectory(const std::string& rootDir = ".");

    void clear();

    const ObjectContainerDesc* findObjectContainer(const std::string& name) const;
    const EntityDesc* findEntity(const std::string& name) const;

    // File path of the prefab (.pre) registered under `name`, or nullptr if unknown. Lets a
    // nested "Prefab <name>" reference resolve and load another prefab file.
    const std::string* findPrefab(const std::string& name) const;

    // Registers (or updates) a prefab name -> .pre path without rescanning the asset tree, so a
    // just-saved prefab is immediately resolvable as a nested "Prefab <name>" reference.
    void addPrefab(const std::string& name, const std::string& path);

    // Name of the root entity/prefab declared in a given file, looked up by file name only (case-
    // insensitive, directories ignored). Lets a dropped file path resolve to its spawnable root
    // without re-reading the file. Returns nullptr if no such file (or no root in it) was scanned.
    const std::string* findRootForFile(const std::string& fileName) const;

    const std::unordered_map<std::string, ObjectContainerDesc>& getObjectContainers() const { return m_objectContainers; }
    const std::unordered_map<std::string, EntityDesc>& getEntities() const { return m_entities; }

private:

    void registerFile(const std::string& path);

    std::unordered_map<std::string, ObjectContainerDesc> m_objectContainers;
    std::unordered_map<std::string, EntityDesc> m_entities;
    std::unordered_map<std::string, std::string> m_prefabs; // name -> .pre file path
    std::unordered_map<std::string, std::string> m_fileRoot; // lowercased file name -> root entity/prefab name
};


export namespace Globals
{
    AssetRegistry assetRegistry;
}
