module Entity;

import Core;
import Core.glm;
import Core.Log;

import RendererVK;
import :Entity;
import :Component;
import File;

import :AssetRegistry;
import :ObjectDescription;

bool World::initialize()
{
    Globals::assetRegistry.scanDirectory();
    return true;
}

static RendererVKLayout::EPipelineIndex parsePipeline(const std::string& name)
{
    using P = RendererVKLayout::EPipelineIndex;
    if (name == "LitTransparent")   return P::LitTransparent;
    if (name == "UnlitOpaque")      return P::UnlitOpaque;
    if (name == "UnlitTransparent") return P::UnlitTransparent;
    if (name == "Sky")              return P::Sky;
    if (name == "WireframeTransparent") return P::WireframeTransparent;
    if (name == "GizmoUI")          return P::GizmoUI;
    if (name == "GizmoWorld")       return P::GizmoWorld;
    return P::LitOpaque;
}

ObjectContainer* World::loadContainer(const ObjectContainerDesc& desc)
{
    if (auto it = m_containers.find(desc.name); it != m_containers.end())
        return it->second.get();

    std::unique_ptr<ISceneData> sceneData = desc.procedural
        ? ISceneData::createProceduralLoader()
        : ISceneData::createAssimpLoader();
    if (!sceneData->initialize(desc.path.c_str(), desc.mergeNodes, desc.preTransformVertices))
    {
        Log::warning("Scene: failed to load '" + desc.path + "' for ObjectContainer '" + desc.name + "'");
        return nullptr;
    }

    auto container = std::make_unique<ObjectContainer>();
    if (desc.materialOverrides.present)
    {
        const MaterialOverridesDesc& mo = desc.materialOverrides;
        ObjectContainer::MaterialOverrides overrides;
        if (!mo.pipeline.empty())
            overrides.pipelineIdx = parsePipeline(mo.pipeline);
        overrides.excludeFromRayTracing = mo.excludeFromRayTracing;
        overrides.useSceneTextures = mo.useSceneTextures;
        if (mo.diffuseTexIdx >= 0)         overrides.diffuseTexIdx = uint16(mo.diffuseTexIdx);
        if (mo.normalTexIdx >= 0)          overrides.normalTexIdx = uint16(mo.normalTexIdx);
        if (mo.metalRoughnessTexIdx >= 0)  overrides.metalRoughnessTexIdx = uint16(mo.metalRoughnessTexIdx);
        container->initialize(*sceneData, &overrides);
    }
    else
    {
        container->initialize(*sceneData);
    }
    ObjectContainer* ptr = container.get();
    m_containers.emplace(desc.name, std::move(container));
    return ptr;
}

ObjectContainer* World::getOrLoadContainer(const std::string& name)
{
    if (auto it = m_containers.find(name); it != m_containers.end())
        return it->second.get();
    if (const ObjectContainerDesc* desc = Globals::assetRegistry.findObjectContainer(name))
        return loadContainer(*desc);
    Log::warning("Scene: unknown ObjectContainer reference '" + name + "'");
    return nullptr;
}

EntityPtr World::spawn(const std::string& name, const Transform& base)
{
    if (std::shared_ptr<const EntitySpawnTemplate> tmpl = getOrBuildPrefabTemplate(name))
        return Entity::create(*tmpl, base);
    return EntityPtr{};
}

void World::reloadPrefabs()
{
    for (auto& [name, tmpl] : m_templates)
        m_retiredTemplates.push_back(std::move(tmpl));
    m_templates.clear();
}

void World::invalidatePrefab(const std::string& name)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
    {
        m_retiredTemplates.push_back(std::move(it->second)); // kept alive for live entities
        m_templates.erase(it);
    }
}

static const AssetNode* findComponentNode(const AssetNode& node, const char* name)
{
    for (const AssetNode* comp : node.findAll("Component"))
        if (comp->asString() == name)
            return comp;
    return nullptr;
}

static bool keyIs(const AssetNode& node, std::string_view key)
{
    if (node.key.size() != key.size())
        return false;
    for (size_t i = 0; i < key.size(); ++i)
    {
        const char a = node.key[i] | 0x20, b = key[i] | 0x20; // ASCII lower
        if (a != b)
            return false;
    }
    return true;
}

static Transform readNodeTransform(const AssetNode& node)
{
    Transform t;
    t.scale = 1.0f;
    if (const AssetNode* n = node.find("Position")) t.pos = n->asVec3();
    if (const AssetNode* n = node.find("Rotation")) t.quat = glm::quat(glm::radians(n->asVec3()));
    if (const AssetNode* n = node.find("Scale"))    t.scale = n->asFloat(0, 1.0f);
    return t;
}

std::shared_ptr<RenderComponent::SpawnInfo> World::buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName)
{
    // Preferred form: a named spawnable (StaticMesh/SkinnedMesh) declared in a .oc, which resolves to a
    // container + node. Falls back to the legacy `ObjectContainer` + `Node` form.
    const AssetNode* staticNode  = renderNode.find("StaticMesh");
    const AssetNode* skinnedNode = renderNode.find("SkinnedMesh");
    const AssetNode* spawnNode   = staticNode ? staticNode : skinnedNode;

    std::string containerName;
    std::string nodePath;
    bool skinned = false;
    std::string spawnableName;

    if (spawnNode)
    {
        spawnableName = spawnNode->asString();
        const SpawnableDesc* desc = Globals::assetRegistry.findSpawnable(spawnableName);
        if (!desc)
        {
            Log::warning("Scene: entity '" + ownerName + "' references unknown mesh '" + spawnableName + "'");
            return nullptr;
        }
        // An explicit ObjectContainer alongside the name disambiguates if the same name exists in several.
        const AssetNode* containerNode = renderNode.find("ObjectContainer");
        containerName = containerNode ? containerNode->asString() : desc->containerName;
        nodePath = desc->node;
        skinned = desc->skinned;
    }
    else if (const AssetNode* containerNode = renderNode.find("ObjectContainer"))
    {
        containerName = containerNode->asString();
        nodePath = renderNode.find("Node") ? renderNode.find("Node")->asString() : std::string();
    }
    else
    {
        return nullptr;
    }

    ObjectContainer* container = getOrLoadContainer(containerName);
    if (!container)
        return nullptr;

    auto info = std::make_shared<RenderComponent::SpawnInfo>();
    info->container = container;
    info->containerName = containerName; // kept so an inline entity re-serializes its mesh
    info->skinned = skinned;
    info->spawnableName = spawnableName;

    if (nodePath.empty() || nodePath == "ROOT")
        info->nodeIdx = NodeSpawnIdx_ROOT;
    else if (NodeSpawnIdx idx = container->getSpawnIdxForPath(nodePath); idx != NodeSpawnIdx_INVALID)
        info->nodeIdx = idx;
    else
        Log::warning("Scene: entity '" + ownerName + "' references unknown node '" + nodePath + "', using ROOT");
    info->nodePath = nodePath.empty() ? "ROOT" : nodePath;
    info->localTransform = readNodeTransform(renderNode); // mesh offset within the entity
    return info;
}

void writeRenderSpawnInfo(const RenderComponent::SpawnInfo& info, AssetNode& out)
{
    if (!info.container)
        return;
    if (!info.spawnableName.empty())
        out.set(info.skinned ? "SkinnedMesh" : "StaticMesh", info.spawnableName);
    else
    {
        out.set("ObjectContainer", info.containerName);
        out.set("Node", info.nodePath);
    }
    const Transform& lt = info.localTransform;
    if (lt.pos != glm::vec3(0.0f))         out.set("Position", lt.pos);
    if (lt.quat != glm::quat(1, 0, 0, 0))  out.set("Rotation", glm::degrees(glm::eulerAngles(lt.quat)));
    if (lt.scale != 1.0f)                  out.set("Scale", lt.scale);
}

std::shared_ptr<SceneComponent::SpawnInfo> World::buildSceneSpawnInfo(const AssetNode& sceneNode)
{
    auto info = std::make_shared<SceneComponent::SpawnInfo>();
    if (const AssetNode* n = sceneNode.find("Enabled")) info->enabled = n->asBool();

    for (const AssetNode& child : sceneNode.children)
    {
        std::shared_ptr<const EntitySpawnTemplate> childTmpl;
        if (keyIs(child, "Entity"))
            childTmpl = buildInlineTemplate(child);
        else if (keyIs(child, "Prefab"))
            childTmpl = getOrBuildPrefabTemplate(child.asString());
        else
            continue;
        if (!childTmpl)
            continue;

        SceneComponent::SpawnInfo::ChildSpawnInfo ci;
        ci.tmpl = std::move(childTmpl);
        ci.localTransform = readNodeTransform(child);
        if (const AssetNode* n = child.find("Name")) ci.name = n->asString();
        info->children.push_back(std::move(ci));
    }
    return info;
}

void World::buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl)
{
    tmpl.defaultTransform = readNodeTransform(node); // the declaration's authored placement
    const AssetNode* nameNode = node.find("Name");
    tmpl.displayName = nameNode ? nameNode->asString() : node.asString();

    uint16 typeBits = 0;

    static_assert(EComponentID_Scene == 0);
    if (const AssetNode* sceneNode = findComponentNode(node, "Scene"))
        if (std::shared_ptr<SceneComponent::SpawnInfo> info = buildSceneSpawnInfo(*sceneNode))
        {
            typeBits |= uint16(1 << EComponentID_Scene);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    static_assert(EComponentID_Render == 3);
    if (const AssetNode* renderNode = findComponentNode(node, "Render"))
        if (std::shared_ptr<RenderComponent::SpawnInfo> info = buildRenderSpawnInfo(*renderNode, tmpl.displayName))
        {
            typeBits |= uint16(1 << EComponentID_Render);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    tmpl.archetype = makeEntityArchetype(typeBits);
}

std::shared_ptr<const EntitySpawnTemplate> World::cacheTemplate(const std::string& name, const std::string& sourceFile, const AssetNode& node)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
        return it->second;

    m_buildingTemplates.insert(name);
    auto tmpl = std::make_shared<EntitySpawnTemplate>();
    tmpl->sourceFile = sourceFile;
    tmpl->prefabName = name; // a registered prefab name, so a re-serialized instance writes "Prefab <name>"
    buildTemplate(node, *tmpl);
    m_buildingTemplates.erase(name);

    m_templates.emplace(name, tmpl); // heap-owned: address stays stable for child back-pointers
    return tmpl;
}

std::shared_ptr<const EntitySpawnTemplate> World::buildInlineTemplate(const AssetNode& node)
{
    auto tmpl = std::make_shared<EntitySpawnTemplate>();
    buildTemplate(node, *tmpl);
    return tmpl;
}

std::shared_ptr<const EntitySpawnTemplate> World::buildFileTemplate(const std::string& path)
{
    AssetNode doc;
    std::string error;
    if (!loadAssetFile(path, doc, error))
    {
        Log::warning("Scene: asset load failed: " + error);
        return nullptr;
    }

    for (const AssetNode& decl : doc.children)
        if (keyIs(decl, "Prefab"))
            return cacheTemplate(decl.asString(), path, decl);

    Log::warning("Scene: asset '" + path + "' declared no prefab");
    return nullptr;
}

std::shared_ptr<const EntitySpawnTemplate> World::getOrBuildPrefabTemplate(const std::string& name)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
        return it->second; // cache hit: no asset file touched

    if (m_buildingTemplates.contains(name))
    {
        Log::warning("Scene: prefab cycle detected at '" + name + "', skipping");
        return nullptr;
    }

    if (const std::string* prefabPath = Globals::assetRegistry.findPrefab(name))
    {
        if (std::shared_ptr<const EntitySpawnTemplate> tmpl = buildFileTemplate(*prefabPath))
            return tmpl; // its declared root name == `name`
        Log::warning("Scene: prefab '" + name + "' not declared in '" + *prefabPath + "', skipping");
        return nullptr;
    }

    Log::warning("Scene: references unknown prefab '" + name + "', skipping");
    return nullptr;
}

EntityPtr World::spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform)
{
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(path, ec);
    const std::string fileName = (ec || relativePath.empty()) ? path : relativePath.string();

    const std::string* rootName = Globals::assetRegistry.findRootForFile(fileName);
    std::shared_ptr<const EntitySpawnTemplate> tmpl = rootName ? getOrBuildPrefabTemplate(*rootName) : buildFileTemplate(fileName);
    if (!tmpl)
        return EntityPtr{};

    const Transform& dt = tmpl->defaultTransform;
    const glm::vec3 pos = overrideDefaultTransform ? base.pos : dt.pos;
    return Entity::create(*tmpl, Transform(pos, dt.scale, dt.quat));
}

EntityPtr World::createEmptyEntity(const std::string& name)
{
    // A blank Scene-only template with no prefabName: Entity::create leaves prefabInstance false, so the
    // entity is editable and serializes inline. Cached (and kept across reloadPrefabs) so its address
    // stays stable for the entities that point at it.
    if (!m_emptyTemplate)
    {
        m_emptyTemplate = std::make_shared<EntitySpawnTemplate>();
        m_emptyTemplate->archetype = makeEntityArchetype(1 << EComponentID_Scene);
        m_emptyTemplate->spawnInfos.push_back(std::make_shared<SceneComponent::SpawnInfo>());
        m_emptyTemplate->displayName = "Entity";
    }
    EntityPtr entity = Entity::create(*m_emptyTemplate, Transform());
    entity->displayName = name;
    return entity;
}
