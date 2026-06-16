module Entity.Prefab;

import Core;
import Core.glm;
import Entity.Component;
import File.AssetParser;
import File.FileSystem;

// Writes one entity (its header fields, each present component, and its children) into `node`.
static void entityToNode(Entity* entity, AssetNode& node)
{
    node.key = "Entity";
    if (!entity->name.empty())        node.set("Name", entity->name);
    if (!entity->sourceAsset.empty()) node.set("Source", entity->sourceAsset);
    node.set("Position", entity->pos);
    node.set("Rotation", glm::degrees(glm::eulerAngles(entity->rot)));
    node.set("Scale", entity->scale);

    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
    {
        if (!(entity->typeBits & (1 << i)))
            continue;
        AssetNode& comp = node.addChild("Component");
        comp.values.emplace_back(componentTypeName(EComponentID(i)));
        serializeComponent(entity, EComponentID(i), comp);
    }

    if (SceneComponent* sc = getComponent<SceneComponent>(entity); sc && !sc->children.empty())
    {
        AssetNode& childrenNode = node.addChild("Children");
        for (const EntityPtr& child : sc->children)
            entityToNode(child, childrenNode.addChild("Entity"));
    }
}

bool savePrefab(Entity* root, const std::string& path)
{
    if (!root)
        return false;
    AssetNode doc; // synthetic root; its children are the top-level declarations
    entityToNode(root, doc.addChild("Entity"));
    return FileSystem::writeFileStr(path, writeAssetText(doc));
}
