module Entity.AssetRegistry;

import Core.Log;
import File.AssetParser;

namespace Scene
{
    static char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

    static bool iequals(std::string_view a, std::string_view b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (lower(a[i]) != lower(b[i]))
                return false;
        return true;
    }

    // Extensions scanned for declarations. Both parse with the same grammar; the declaration
    // keyword (not the extension) decides what kind of object is registered, so a file of either
    // extension may contain any mix of declarations.
    static constexpr const char* s_assetExtensions[] = { ".oc", ".ent" };

    static bool isAssetFile(const std::filesystem::path& path)
    {
        const std::string ext = path.extension().string();
        for (const char* known : s_assetExtensions)
            if (iequals(ext, known))
                return true;
        return false;
    }

    static std::string toLower(std::string s)
    {
        for (char& c : s)
            c = lower(c);
        return s;
    }

    // Key for m_fileObjects: the path relative to the scan root, lowercased and lexically normalized
    // (collapses the leading ".\" the directory iterator emits and unifies separators) so a dropped
    // path resolved with std::filesystem::relative matches the scanned entry.
    static std::string fileKey(const std::filesystem::path& path)
    {
        return toLower(path.lexically_normal().string());
    }

    void AssetRegistry::clear()
    {
        m_objectContainers.clear();
        m_entities.clear();
        m_fileObjects.clear();
    }

    void AssetRegistry::scanDirectory(const std::string& rootDir)
    {
        clear();

        std::error_code ec;
        auto iter = std::filesystem::recursive_directory_iterator(
            rootDir, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec)
        {
            Log::warning("AssetRegistry: could not scan directory: " + rootDir);
            return;
        }

        for (const std::filesystem::directory_entry& entry : iter)
        {
            if (entry.is_regular_file(ec) && isAssetFile(entry.path()))
                registerFile(entry.path().string());
        }
    }

    void AssetRegistry::registerFile(const std::string& path)
    {
        AssetNode root;
        std::string error;
        if (!loadAssetFile(path, root, error))
        {
            Log::warning("AssetRegistry: " + error);
            return;
        }

        const std::string fileName = fileKey(path);
        std::vector<std::string>& fileObjects = m_fileObjects[fileName];

        for (const AssetNode& decl : root.children)
        {
            if (iequals(decl.key, "ObjectContainer"))
            {
                ObjectContainerDesc desc;
                if (!toObjectContainerDesc(decl, desc))
                    continue;
                if (desc.name.empty())
                {
                    Log::warning("AssetRegistry: unnamed ObjectContainer in " + path);
                    continue;
                }
                const std::string name = desc.name;
                if (!m_objectContainers.try_emplace(desc.name, std::move(desc)).second)
                    Log::warning("AssetRegistry: duplicate ObjectContainer '" + decl.asString(0) + "' (keeping first), in " + path);
                else
                    fileObjects.push_back(name);
            }
            else if (iequals(decl.key, "Entity"))
            {
                EntityDesc desc;
                if (!toEntityDesc(decl, desc))
                    continue;
                if (desc.name.empty())
                {
                    Log::warning("AssetRegistry: unnamed Entity in " + path);
                    continue;
                }
                const std::string name = desc.name;
                if (!m_entities.try_emplace(desc.name, std::move(desc)).second)
                    Log::warning("AssetRegistry: duplicate Entity '" + decl.asString(0) + "' (keeping first), in " + path);
                else
                    fileObjects.push_back(name);
            }
            else
            {
                Log::warning("AssetRegistry: unknown declaration '" + decl.key + "' in " + path);
            }
        }
    }

    const ObjectContainerDesc* AssetRegistry::findObjectContainer(const std::string& name) const
    {
        const auto it = m_objectContainers.find(name);
        return it != m_objectContainers.end() ? &it->second : nullptr;
    }

    const EntityDesc* AssetRegistry::findEntity(const std::string& name) const
    {
        const auto it = m_entities.find(name);
        return it != m_entities.end() ? &it->second : nullptr;
    }

    const std::vector<std::string>* AssetRegistry::findObjectsForFile(const std::string& fileName) const
    {
        const auto it = m_fileObjects.find(fileKey(fileName));
        return it != m_fileObjects.end() ? &it->second : nullptr;
    }
}
