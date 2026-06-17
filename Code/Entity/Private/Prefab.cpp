module Entity.Prefab;

import Core;
import Core.glm;
import Entity.Component;
import Entity.AssetRegistry;
import File.AssetParser;
import File.FileSystem;

static void writeSceneChild(Entity* child, AssetNode& sceneComp);

// Writes the name (when it differs from the token already on the node's own line) and transform
// overrides. `tokenName` is the prefab id or the inline-entity/prefab reference on that line.
static void writeOverrides(Entity* entity, AssetNode& node, const std::string& tokenName)
{
    if (!entity->name.empty() && entity->name != tokenName)
        node.set("Name", entity->name);
    node.set("Position", entity->pos);
    node.set("Rotation", glm::degrees(glm::eulerAngles(entity->rot)));
    node.set("Scale", entity->scale);
}

// Writes a RenderNode component's intrinsic data (its mesh source) from the entity's spawn info, so an
// inline entity round-trips its mesh. The mesh-offset transform is written only when non-default.
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

// Writes the editor-authored overlay shared by a prefab definition and an inline entity: the overrides
// above, each component's intrinsic data (a RenderNode mesh, a Scene block's child hierarchy), and any
// per-component variable tweaks. The component SET is implied by the data written and restored on load.
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

        // RenderNode records its mesh source; Scene carries the editor-built child hierarchy.
        if (id == EComponentID_Render)
            writeRenderNode(entity, comp);
        else if (id == EComponentID_Scene)
            if (SceneComponent* sc = getComponent<SceneComponent>(entity))
                for (const EntityPtr& child : sc->children)
                    writeSceneChild(child, comp);

        // Drop components with nothing to record (no data, no override variables, no children).
        if (comp.children.empty())
            continue;
        node.children.push_back(std::move(comp));
    }
}

// Writes one Scene-component child. A child spawned from a registered prefab is a nested prefab
// instance — written as a `Prefab <name>` reference with only its placement overrides (its contents
// live in the referenced .pre). Otherwise it is an inline `Entity <name>` defined fully in place.
static void writeSceneChild(Entity* child, AssetNode& sceneComp)
{
    const std::string& source = entityPrefabName(child); // the prefab name it spawned from, or empty if inline
    if (!source.empty() && Globals::assetRegistry.findPrefab(source))
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
