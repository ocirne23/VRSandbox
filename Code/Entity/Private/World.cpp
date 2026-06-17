module Entity.World;

import Core;
import Core.glm;
import Core.Log;

import RendererVK;
import Entity;
import Entity.Component;
import File.ISceneData;
import File.AssetParser;

import Entity.AssetRegistry;
import Entity.ObjectDescription;

bool World::initialize()
{
    Globals::assetRegistry.scanDirectory();

    // Build a spawn template for every entity up front, here, so spawning is a pure lookup afterwards
    // (and the ObjectContainers they reference are loaded now rather than mid-spawn). Prefabs are
    // built the same way, but lazily on first reference. Both land in the same template cache.
    for (const auto& [name, desc] : Globals::assetRegistry.getEntities())
        cacheTemplate(name, desc.filePath, desc.node);

    return true;
}

static RendererVKLayout::EPipelineIndex parsePipeline(const std::string& name)
{
    using P = RendererVKLayout::EPipelineIndex;
    if (name == "LitTransparent")   return P::LitTransparent;
    if (name == "UnlitOpaque")      return P::UnlitOpaque;
    if (name == "UnlitTransparent") return P::UnlitTransparent;
    if (name == "Sky")              return P::Sky;
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
    // Entities are pre-built (cache hit); a prefab name resolves and builds here. Either way one call.
    if (const EntitySpawnTemplate* tmpl = getOrBuildTemplate(name))
        return spawnFromTemplate(*tmpl, base);
    return EntityPtr{};
}

// ---- spawn templates -----------------------------------------------------------------------------
// An entity (".ent") and a prefab (".pre") are the same kind of thing: a named entity declaration that
// may carry a RenderNode (a mesh) and/or a Scene block (children to attach on spawn). Both are turned
// into an EntitySpawnTemplate by buildTemplate, cached by name, and spawned by spawnFromTemplate.
// Entity templates are pre-built at init; prefab ones build lazily on first reference. A referenced
// file is read at most once (it builds every declaration it contains). Spawning is then pure lookup.

// The "Component <name>" child of a declaration node, or null if absent.
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

// A node's authored placement (Position/Rotation/Scale). Positions are stored relative to `origin`
// (the owning declaration's own position) so spawning is a pure compose onto the spawn transform.
static Transform readNodeTransform(const AssetNode& node, const glm::vec3& origin)
{
    Transform t;
    t.scale = 1.0f;
    if (const AssetNode* n = node.find("Position")) t.pos = n->asVec3();
    if (const AssetNode* n = node.find("Rotation")) t.quat = glm::quat(glm::radians(n->asVec3()));
    if (const AssetNode* n = node.find("Scale"))    t.scale = n->asFloat(0, 1.0f);
    t.pos -= origin;
    return t;
}

std::shared_ptr<RenderComponent::SpawnInfo> World::buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName)
{
    const AssetNode* containerNode = renderNode.find("ObjectContainer");
    ObjectContainer* container = containerNode ? getOrLoadContainer(containerNode->asString()) : nullptr;
    if (!container)
        return nullptr;

    auto info = std::make_shared<RenderComponent::SpawnInfo>();
    info->container = container;

    const std::string nodePath = renderNode.find("Node") ? renderNode.find("Node")->asString() : std::string();
    if (nodePath.empty() || nodePath == "ROOT")
        info->nodeIdx = NodeSpawnIdx_ROOT;
    else if (NodeSpawnIdx idx = container->getSpawnIdxForPath(nodePath); idx != NodeSpawnIdx_INVALID)
        info->nodeIdx = idx;
    else
        Log::warning("Scene: entity '" + ownerName + "' references unknown node '" + nodePath + "', using ROOT");
	info->nodePath = nodePath.empty() ? "ROOT" : nodePath;
    info->localTransform = readNodeTransform(renderNode, glm::vec3(0.0f)); // mesh offset within the entity
    return info;
}

std::shared_ptr<SceneComponent::SpawnInfo> World::buildSceneSpawnInfo(const AssetNode& sceneNode, const glm::vec3& origin)
{
    auto info = std::make_shared<SceneComponent::SpawnInfo>();
    if (const AssetNode* n = sceneNode.find("Enabled")) info->enabled = n->asBool();

    // Each child references another template by name. The "Entity"/"Prefab" keyword only hints where
    // it's declared; both resolve identically through getOrBuildTemplate. Authoring order is kept.
    for (const AssetNode& child : sceneNode.children)
    {
        if (!keyIs(child, "Entity") && !keyIs(child, "Prefab"))
            continue;
        const EntitySpawnTemplate* childTmpl = getOrBuildTemplate(child.asString());
        if (!childTmpl)
            continue;

        SceneComponent::SpawnInfo::ChildSpawnInfo ci;
        ci.tmpl = childTmpl;
        ci.localTransform = readNodeTransform(child, origin);
        if (const AssetNode* n = child.find("Name")) ci.name = n->asString();
        info->children.push_back(std::move(ci));
    }
    return info;
}

void World::buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl)
{
    tmpl.defaultTransform = readNodeTransform(node, glm::vec3(0.0f)); // the declaration's authored placement
    const AssetNode* nameNode = node.find("Name");
    tmpl.name = nameNode ? nameNode->asString() : node.asString();

    // Resolve each component the declaration carries to its parse-once SpawnInfo, indexed by id.
    uint16 typeBits = 0;

    static_assert(EComponentID_Scene == 0);
    if (const AssetNode* sceneNode = findComponentNode(node, "Scene"))
        if (std::shared_ptr<SceneComponent::SpawnInfo> info = buildSceneSpawnInfo(*sceneNode, tmpl.defaultTransform.pos))
        {
            typeBits |= uint16(1 << EComponentID_Scene);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    static_assert(EComponentID_Render == 3);
    if (const AssetNode* renderNode = findComponentNode(node, "RenderNode"))
        if (std::shared_ptr<RenderComponent::SpawnInfo> info = buildRenderSpawnInfo(*renderNode, tmpl.sourceAsset))
        {
            typeBits |= uint16(1 << EComponentID_Render);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    // One spawn-info slot per set component bit, in id order (null where a present component has none).
    tmpl.archetype = makeEntityArchetype(typeBits);
}

const EntitySpawnTemplate* World::cacheTemplate(const std::string& name, const std::string& filePath, const AssetNode& node)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
        return it->second.get();

    // Mark in-progress before building so a self/cyclic child reference (resolved while building the
    // Scene children) is caught by getOrBuildTemplate rather than recursing forever.
    m_buildingTemplates.insert(name);
    auto tmpl = std::make_unique<EntitySpawnTemplate>();
    tmpl->sourceAsset = filePath;
    buildTemplate(node, *tmpl);
    m_buildingTemplates.erase(name);

    const EntitySpawnTemplate* ptr = tmpl.get();
    m_templates.emplace(name, std::move(tmpl)); // heap-owned: address stays stable for child back-pointers
    return ptr;
}

const EntitySpawnTemplate* World::buildFileTemplate(const std::string& path)
{
    AssetNode doc;
    std::string error;
    if (!loadAssetFile(path, doc, error))
    {
        Log::warning("Scene: asset load failed: " + error);
        return nullptr;
    }

    // A spawnable file declares a single root: build the template for its first "Entity"/"Prefab".
    for (const AssetNode& decl : doc.children)
        if (keyIs(decl, "Entity") || keyIs(decl, "Prefab"))
            return cacheTemplate(decl.asString(), path, decl);

    Log::warning("Scene: asset '" + path + "' declared no spawnable entity or prefab");
    return nullptr;
}

const EntitySpawnTemplate* World::getOrBuildTemplate(const std::string& name)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
        return it->second.get(); // cache hit: no asset file touched

    if (m_buildingTemplates.contains(name))
    {
        Log::warning("Scene: template cycle detected at '" + name + "', skipping");
        return nullptr;
    }

    // An entity declaration builds straight from its (already-parsed) node; a prefab reads its file,
    // which builds its single root declaration. Either way the result is cached under `name`.
    if (const EntityDesc* desc = Globals::assetRegistry.findEntity(name))
        return cacheTemplate(name, desc->name, desc->node);

    if (const std::string* prefabPath = Globals::assetRegistry.findPrefab(name))
    {
        if (const EntitySpawnTemplate* tmpl = buildFileTemplate(*prefabPath))
            return tmpl; // its declared root name == `name`
        Log::warning("Scene: prefab '" + name + "' not declared in '" + *prefabPath + "', skipping");
        return nullptr;
    }

    Log::warning("Scene: references unknown object '" + name + "', skipping");
    return nullptr;
}

EntityPtr World::spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform)
{
    // Resolve the dropped file to its single root template: cache-first via the registry's file map
    // (mapped at scan time, no re-read), falling back to a single parse for a file the registry hasn't
    // scanned (e.g. a just-saved prefab). The drop payload is an absolute path while the registry keys
    // by path relative to its scan root (the Assets working dir), so relativize for the lookup.
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(path, ec);
    const std::string fileName = (ec || relativePath.empty()) ? path : relativePath.string();

    const std::string* rootName = Globals::assetRegistry.findRootForFile(fileName);
    const EntitySpawnTemplate* tmpl = rootName ? getOrBuildTemplate(*rootName) : buildFileTemplate(fileName);
    if (!tmpl)
        return EntityPtr{};

    // Compose the root's authored transform onto `base`. When anchoring (a viewport drop) the authored
    // position is cancelled so it lands exactly at `base`; otherwise (a hierarchy drop) it is kept.
    const Transform& dt = tmpl->defaultTransform;
    const glm::vec3 pos = overrideDefaultTransform ? glm::vec3(0.0f) : dt.pos;
    return spawnFromTemplate(*tmpl, composeTransform(base, Transform(pos, dt.scale, dt.quat)));
}