module Entity.Prefab;

import Core;
import Core.Log;
import Core.glm;
import Entity.Component;
import Entity.AssetRegistry;
import File.AssetParser;
import File.FileSystem;

static void writeSceneChild(Entity* child, AssetNode& sceneComp);

static void writeOverrides(Entity* entity, AssetNode& node, const std::string& tokenName)
{
    if (!entity->name.empty() && entity->name != tokenName)
        node.set("Name", entity->name);
    node.set("Position", entity->pos);
    node.set("Rotation", glm::degrees(glm::eulerAngles(entity->rot)));
    node.set("Scale", entity->scale);
}

static void writeRenderNode(Entity* entity, AssetNode& comp)
{
    const RenderComponent::SpawnInfo* si = getRenderSpawnInfo(entity);
    if (!si || !si->container)
        return;
    comp.set("ObjectContainer", si->containerName);
    comp.set("Node", si->nodePath);
    const Transform& lt = si->localTransform;
    if (lt.pos != glm::vec3(0.0f))         comp.set("Position", lt.pos);
    if (lt.quat != glm::quat(1, 0, 0, 0))  comp.set("Rotation", glm::degrees(glm::eulerAngles(lt.quat)));
    if (lt.scale != 1.0f)                  comp.set("Scale", lt.scale);
}

static void writeEntityBody(Entity* entity, AssetNode& node, const std::string& tokenName)
{
    writeOverrides(entity, node, tokenName);

    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
    {
        if (!(entity->typeBits & (1 << i)))
            continue;
        const EComponentID id = EComponentID(i);

        AssetNode comp;
        comp.key = "Component";
        comp.values.emplace_back(componentTypeName(id));
        entity->serializeComponent(id, comp); // appends overridable variables as children

        if (id == EComponentID_Render)
            writeRenderNode(entity, comp);
        else if (id == EComponentID_Scene)
            if (SceneComponent* sc = getComponent<SceneComponent>(entity))
                for (const EntityPtr& child : sc->children)
                    writeSceneChild(child, comp);

        if (comp.children.empty())
            continue;
        node.children.push_back(std::move(comp));
    }
}

static void writeSceneChild(Entity* child, AssetNode& sceneComp)
{
    const std::string& source = child->getPrefabName(); // the prefab name it spawned from, or empty if inline
    if (child->isPrefabInstance() && !source.empty() && Globals::assetRegistry.findPrefab(source))
    {
        AssetNode& node = sceneComp.addChild("Prefab");
        node.values.emplace_back(source);
        writeOverrides(child, node, source);
    }
    else
    {
        const std::string token = child->name.empty() ? std::string("Entity") : child->name;
        AssetNode& node = sceneComp.addChild("Entity");
        node.values.emplace_back(token);
        writeEntityBody(child, node, token);
    }
}

static bool keyEq(const std::string& a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < b.size(); ++i)
        if ((a[i] | 0x20) != (b[i] | 0x20)) // ASCII lower; keys are human-authored
            return false;
    return true;
}

// Gather every prefab reference (`Prefab <name>`) nested anywhere under `node`. References carry only
// overrides, never nested declarations, so we don't descend into a reference node itself.
static void collectAssetPrefabRefs(const AssetNode& node, std::vector<std::string>& out)
{
    for (const AssetNode& child : node.children)
    {
        if (keyEq(child.key, "Prefab"))
            out.push_back(child.asString());
        else
            collectAssetPrefabRefs(child, out);
    }
}

// Does prefab `name` (transitively, via its on-disk file) reach `target`?
static bool prefabFileReaches(const std::string& name, const std::string& target, std::unordered_set<std::string>& visited)
{
    if (name == target)
        return true;
    if (!visited.insert(name).second)
        return false; // already explored (guards against a pre-existing cyclic graph on disk)

    const std::string* path = Globals::assetRegistry.findPrefab(name);
    if (!path)
        return false;

    AssetNode doc;
    std::string error;
    if (!loadAssetFile(*path, doc, error))
        return false;

    std::vector<std::string> refs;
    for (const AssetNode& decl : doc.children)
        if (keyEq(decl.key, "Prefab"))
            collectAssetPrefabRefs(decl, refs);

    for (const std::string& ref : refs)
        if (prefabFileReaches(ref, target, visited))
            return true;
    return false;
}

// Prefab instances serialize as a bare reference (not their subtree), so collect the names they spawn;
// descend only through inline entities, whose bodies are written out in full.
static void collectLivePrefabRefs(Entity* entity, std::vector<std::string>& out)
{
    SceneComponent* sc = getComponent<SceneComponent>(entity);
    if (!sc)
        return;
    for (const EntityPtr& child : sc->children)
    {
        if (child->isPrefabInstance() && !child->getPrefabName().empty())
            out.push_back(child->getPrefabName());
        else
            collectLivePrefabRefs(child.get(), out);
    }
}

bool prefabWouldCycle(Entity* root, const std::string& id)
{
    std::vector<std::string> refs;
    collectLivePrefabRefs(root, refs);

    std::unordered_set<std::string> visited;
    for (const std::string& ref : refs)
        if (prefabFileReaches(ref, id, visited)) // direct (ref == id) or transitive through ref's file
            return true;
    return false;
}

bool savePrefab(Entity* root, const std::string& path)
{
    if (!root)
        return false;
    const std::string id = std::filesystem::path(path).stem().string();
    if (prefabWouldCycle(root, id))
    {
        Log::warning("Prefab: refusing to save '" + id + "' — it contains a prefab instance of itself (cycle)");
        return false;
    }

    AssetNode doc; // synthetic root; its children are the top-level declarations
    AssetNode& node = doc.addChild("Prefab");
    node.values.emplace_back(id);
    writeEntityBody(root, node, id);

    if (!FileSystem::writeFileStr(path, writeAssetText(doc)))
        return false;

    Globals::assetRegistry.addPrefab(id, path);
    return true;
}
