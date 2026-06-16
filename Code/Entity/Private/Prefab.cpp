module Entity.Prefab;

import Core;
import Core.glm;
import Entity.Component;
import Entity.AssetRegistry;
import File.AssetParser;
import File.FileSystem;

static void writeSceneChild(Entity* child, AssetNode& sceneComp);

// Writes the name (when it differs from the token already on the node's own line) and transform
// overrides. `tokenName` is the prefab id or ".ent"/prefab reference written on that line.
static void writeOverrides(Entity* entity, AssetNode& node, const std::string& tokenName)
{
    if (!entity->name.empty() && entity->name != tokenName)
        node.set("Name", entity->name);
    node.set("Position", entity->pos);
    node.set("Rotation", glm::degrees(glm::eulerAngles(entity->rot)));
    node.set("Scale", entity->scale);
}

// Writes the editor-authored overlay shared by a prefab definition and an entity reference: the
// overrides above, per-component variable tweaks, and — for the Scene component — the child
// hierarchy nested inside it. The component SET itself is not written; it is restored from the
// template (or, for a prefab, is intrinsically Scene) on load.
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
        serializeComponent(entity, id, comp); // appends overridable variables as children

        // The Scene component additionally carries the editor-built child hierarchy.
        if (id == EComponentID_Scene)
            if (SceneComponent* sc = getComponent<SceneComponent>(entity))
                for (const EntityPtr& child : sc->children)
                    writeSceneChild(child, comp);

        // Drop components with nothing to record (no override variables, no children).
        if (comp.children.empty())
            continue;
        node.children.push_back(std::move(comp));
    }
}

// Writes one Scene-component child. An entity whose source names a registered prefab is a nested
// prefab instance — written as a `Prefab <name>` reference with only its placement overrides (its
// contents live in the referenced .pre). Otherwise it is a `Entity <source>` reference with the
// full overlay.
static void writeSceneChild(Entity* child, AssetNode& sceneComp)
{
    if (Globals::assetRegistry.findPrefab(child->sourceAsset))
    {
        AssetNode& node = sceneComp.addChild("Prefab");
        node.values.emplace_back(child->sourceAsset);
        writeOverrides(child, node, child->sourceAsset);
    }
    else
    {
        AssetNode& node = sceneComp.addChild("Entity");
        node.values.emplace_back(child->sourceAsset); // ".ent" template this entity references
        writeEntityBody(child, node, child->sourceAsset);
    }
}

bool savePrefab(Entity* root, const std::string& path)
{
    if (!root)
        return false;
    AssetNode doc; // synthetic root; its children are the top-level declarations
    AssetNode& node = doc.addChild("Prefab");
    // The prefab id is its file stem (a stable, space-free reference token); the display name is
    // written separately as a Name override when it differs.
    const std::string id = std::filesystem::path(path).stem().string();
    node.values.emplace_back(id);
    writeEntityBody(root, node, id);

    if (!FileSystem::writeFileStr(path, writeAssetText(doc)))
        return false;

    // Make the just-saved prefab immediately nestable (resolvable as a "Prefab <id>" reference)
    // without waiting for a full asset rescan.
    Globals::assetRegistry.addPrefab(id, path);
    return true;
}
