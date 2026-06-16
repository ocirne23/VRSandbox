export module Entity.AssetRegistry;

import Core;
import Entity.ObjectDescription;

export namespace Scene
{
    // Scans the asset tree for asset files (.oc / .ent), parses every top-level declaration, and
    // registers it by the name that follows its keyword (e.g. "ObjectContainer sponza" -> "sponza").
    // Names form a flat reference space per kind: a declaration's name is what other declarations
    // (e.g. an Entity's RenderNode component) refer to. File names need not match declaration names,
    // and a single file may declare multiple objects.
    class AssetRegistry final
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

        // Names of every declaration found in a given file, looked up by file name only (case-
        // insensitive, directories ignored). Lets a dropped file path resolve to its spawnable
        // objects without re-reading the file. Returns nullptr if no such file was scanned.
        const std::vector<std::string>* findObjectsForFile(const std::string& fileName) const;

        const std::unordered_map<std::string, ObjectContainerDesc>& getObjectContainers() const { return m_objectContainers; }
        const std::unordered_map<std::string, EntityDesc>& getEntities() const { return m_entities; }

    private:

        void registerFile(const std::string& path);

        std::unordered_map<std::string, ObjectContainerDesc> m_objectContainers;
        std::unordered_map<std::string, EntityDesc> m_entities;
        std::unordered_map<std::string, std::string> m_prefabs; // name -> .pre file path
        std::unordered_map<std::string, std::vector<std::string>> m_fileObjects; // lowercased file name -> declaration names
    };
}

export namespace Globals
{
    Scene::AssetRegistry assetRegistry;
}
