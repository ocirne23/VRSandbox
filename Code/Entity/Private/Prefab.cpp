module Entity.Prefab;

import Core;
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
        serializeComponent(entity, id, comp); // appends overridable variables as children

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
    const std::string id = std::filesystem::path(path).stem().string();
    node.values.emplace_back(id);
    writeEntityBody(root, node, id);

    if (!FileSystem::writeFileStr(path, writeAssetText(doc)))
        return false;

    Globals::assetRegistry.addPrefab(id, path);
    return true;
}
