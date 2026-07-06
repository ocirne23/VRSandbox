module Entity;

import Core;
import Core.Log;
import File;

import :AssetRegistry;
import :ObjectDescription;
import :AnimationDescription;

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

static constexpr const char* s_assetExtensions[] = { ".oc", ".pre", ".anm", ".apl" };

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

static std::string fileKey(const std::filesystem::path& path)
{
    return toLower(path.lexically_normal().string());
}

void AssetRegistry::clear()
{
    m_objectContainers.clear();
    m_clips.clear();
    m_animators.clear();
    m_prefabs.clear();
    m_fileRoot.clear();
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
        {
            auto path = entry.path().string();
            const std::filesystem::path relativePath = std::filesystem::relative(path, ec);
            const std::string filePath = (ec || relativePath.empty()) ? path : relativePath.string();
            registerFile(filePath);
        }
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
            if (!m_objectContainers.try_emplace(desc.name, std::move(desc)).second)
                Log::warning("AssetRegistry: duplicate ObjectContainer '" + decl.asString(0) + "' (keeping first), in " + path);
        }
        else if (iequals(decl.key, "Animation"))
        {
            AnimationClipDesc desc;
            if (!toAnimationClipDesc(decl, desc))
                continue;
            if (desc.name.empty())
            {
                Log::warning("AssetRegistry: unnamed Animation in " + path);
                continue;
            }
            const std::string clipName = desc.name;
            if (!m_clips.try_emplace(clipName, std::move(desc)).second)
                Log::warning("AssetRegistry: duplicate Animation '" + clipName + "' (keeping first), in " + path);
        }
        else if (iequals(decl.key, "Animator"))
        {
            AnimatorDesc desc;
            if (!toAnimatorDesc(decl, desc))
                continue;
            if (desc.name.empty())
            {
                Log::warning("AssetRegistry: unnamed Animator in " + path);
                continue;
            }
            const std::string animatorName = desc.name;
            if (!m_animators.try_emplace(animatorName, std::move(desc)).second)
                Log::warning("AssetRegistry: duplicate Animator '" + animatorName + "' (keeping first), in " + path);
        }
        else if (iequals(decl.key, "Prefab"))
        {
            const std::string name = decl.asString(0);
            if (name.empty())
            {
                Log::warning("AssetRegistry: unnamed Prefab in " + path);
                continue;
            }
            if (!m_prefabs.try_emplace(name, path).second)
            {
                Log::warning("AssetRegistry: duplicate Prefab '" + name + "' (keeping first), in " + path);
                continue;
            }
            if (!m_fileRoot.try_emplace(fileName, name).second)
                Log::warning("AssetRegistry: '" + path + "' declares more than one root prefab "
                    "(keeping '" + m_fileRoot[fileName] + "', ignoring '" + name + "')");
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

const AnimationClipDesc* AssetRegistry::findClip(const std::string& name) const
{
    const auto it = m_clips.find(name);
    return it != m_clips.end() ? &it->second : nullptr;
}

const AnimatorDesc* AssetRegistry::findAnimator(const std::string& name) const
{
    const auto it = m_animators.find(name);
    return it != m_animators.end() ? &it->second : nullptr;
}

const std::string* AssetRegistry::findPrefab(const std::string& name) const
{
    const auto it = m_prefabs.find(name);
    return it != m_prefabs.end() ? &it->second : nullptr;
}

void AssetRegistry::addPrefab(const std::string& name, const std::string& path)
{
    m_prefabs.insert_or_assign(name, path);
}

const std::string* AssetRegistry::findRootForFile(const std::string& fileName) const
{
    const auto it = m_fileRoot.find(fileKey(fileName));
    return it != m_fileRoot.end() ? &it->second : nullptr;
}
