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

    // Build a spawn template for every entity once, here, so spawning is a pure lookup afterwards.
    // Only entities are spawnable; the ObjectContainers they reference are loaded on demand.
    for (const auto& [name, desc] : Globals::assetRegistry.getEntities())
        registerTemplate(desc);

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
    // Pure lookup: every spawnable entity got a template at startup, no asset interpretation here.
    if (auto it = m_spawnTemplates.find(name); it != m_spawnTemplates.end())
        return spawnFromTemplate(*it->second, base);

    Log::warning("Scene: spawn() found no spawnable entity named '" + name + "'");
    return EntityPtr{};
}

// ---- spawn template registration (startup only) ------------------------------------------------
// Resolving an entity (loading its container, looking up the node index, parsing transform
// properties) and computing its archetype happens once, here. Only entities are spawnable;
// ObjectContainers are loaded on demand as dependencies but never get a template of their own.

void World::registerTemplate(const EntityDesc& desc)
{
    EntitySpawnTemplate tmpl;
    uint16 typeBits = 0;

    // Only the RenderNode component maps to an inline component today; the Render bit (and inline
    // storage) is added only if its container resolves.
    std::shared_ptr<RenderComponent::SpawnInfo> renderInfo;
    if (const ComponentDesc* renderDesc = desc.findComponent("RenderNode"))
    {
        if (ObjectContainer* container = getOrLoadContainer(renderDesc->property("ObjectContainer")))
        {
            typeBits |= uint16(1 << EComponentID_Render);
            renderInfo = std::make_shared<RenderComponent::SpawnInfo>();
            renderInfo->container = container;

            const std::string node = renderDesc->property("Node");
            if (node.empty() || node == "ROOT")
                renderInfo->nodeIdx = NodeSpawnIdx_ROOT;
            else if (NodeSpawnIdx idx = container->getSpawnIdxForPath(node); idx != NodeSpawnIdx_INVALID)
                renderInfo->nodeIdx = idx;
            else
                Log::warning("Scene: entity '" + desc.name + "' references unknown node '" + node + "', using ROOT");

            const glm::quat rot = glm::normalize(glm::quat(glm::radians(renderDesc->vec3Property("Rotation"))));
            renderInfo->localTransform = Transform(renderDesc->vec3Property("Position"), renderDesc->floatProperty("Scale", 1.0f), rot);
        }
    }

    tmpl.archetype = makeEntityArchetype(typeBits);
    tmpl.sourceAsset = desc.name; // the name entities spawned from this template re-resolve through
    tmpl.name = desc.name;        // default display name (a ".ent" has no entity-level transform of its own)

    // One spawn-info slot per set component bit, in id order; null where the component has no
    // spawn step (only RenderComponent has one today).
    for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
    {
        if (!(typeBits & (1 << i)))
            continue;
        if (EComponentID(i) == EComponentID_Render)
            tmpl.spawnInfos.push_back(std::move(renderInfo));
        else
            tmpl.spawnInfos.emplace_back();
    }
    // keep-first (an entity may share a name with its container). Heap-owned so the SpawnTemplate
    // address stays stable for the Entity::spawnTemplate back-pointers.
    m_spawnTemplates.emplace(desc.name, std::make_unique<EntitySpawnTemplate>(std::move(tmpl)));
}

// ---- prefab (.pre) loading ---------------------------------------------------------------------
// A prefab file is read exactly once and turned into a cached spawn template (a Scene container whose
// SceneComponent::SpawnInfo lists each child's resolved template + placement). Spawning a prefab —
// top-level or nested — is then a pure recursive walk of templates via spawnFromTemplate, no asset
// reinterpretation. Nested "Prefab <name>" references share the same cached templates.

// The "Component <name>" child of a prefab node, or null if absent.
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

// A child/root node's authored placement. Positions are stored relative to `origin` (the prefab
// root's own position) so spawning is a pure compose onto the drop transform.
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

void World::buildSceneChildren(const AssetNode& sceneNode, const glm::vec3& defOrigin, SceneComponent::SpawnInfo& out)
{
    // Preserve authoring order: an "Entity" is a ".ent" reference, a "Prefab" is a nested prefab.
    for (const AssetNode& child : sceneNode.children)
    {
        const EntitySpawnTemplate* tmpl = nullptr;
        if (keyIs(child, "Entity"))
        {
            const std::string token = child.asString();
            auto it = m_spawnTemplates.find(token);
            if (it == m_spawnTemplates.end())
            {
                Log::warning("Scene: prefab references unknown entity '" + token + "', skipping");
                continue;
            }
            tmpl = it->second.get();
        }
        else if (keyIs(child, "Prefab"))
        {
            tmpl = getOrBuildPrefabTemplate(child.asString());
            if (!tmpl)
                continue;
        }
        else
            continue;

        SceneComponent::SpawnInfo::ChildSpawnInfo info;
        info.tmpl = tmpl;
        info.localTransform = readNodeTransform(child, defOrigin);
        if (const AssetNode* n = child.find("Name")) info.name = n->asString();
        out.children.push_back(std::move(info));
    }
}

void World::buildPrefabTemplate(const AssetNode& def, EntitySpawnTemplate& tmpl)
{
    // A prefab is intrinsically a Scene container; its Scene block holds the child references.
    tmpl.archetype = makeEntityArchetype(1 << EComponentID_Scene);
    tmpl.sourceAsset = def.asString();
    tmpl.defaultTransform = readNodeTransform(def, glm::vec3(0.0f)); // the def's authored placement, baked once
    const AssetNode* nameNode = def.find("Name");
    tmpl.name = nameNode ? nameNode->asString() : def.asString();

    auto sceneInfo = std::make_shared<SceneComponent::SpawnInfo>();
    if (const AssetNode* sceneNode = findComponentNode(def, "Scene"))
    {
        if (const AssetNode* n = sceneNode->find("Enabled")) sceneInfo->enabled = n->asBool();
        // Children's positions are stored relative to the def's own origin, so spawning is a compose.
        buildSceneChildren(*sceneNode, tmpl.defaultTransform.pos, *sceneInfo);
    }
    tmpl.spawnInfos.push_back(std::move(sceneInfo)); // single slot for the lone Scene bit
}

const EntitySpawnTemplate* World::cachePrefabTemplate(const std::string& name, const AssetNode& def)
{
    if (auto it = m_prefabTemplates.find(name); it != m_prefabTemplates.end())
        return it->second.get();

    // Mark as in-progress before building so a self/cyclic nested reference (resolved while building
    // the children) is caught by getOrBuildPrefabTemplate rather than recursing forever.
    m_loadingPrefabs.insert(name);
    auto tmpl = std::make_unique<EntitySpawnTemplate>();
    buildPrefabTemplate(def, *tmpl);
    m_loadingPrefabs.erase(name);

    const EntitySpawnTemplate* ptr = tmpl.get();
    m_prefabTemplates.emplace(name, std::move(tmpl)); // heap-owned: address stays stable for child refs
    return ptr;
}

std::vector<const EntitySpawnTemplate*> World::buildPrefabFileTemplates(const std::string& path)
{
    std::vector<const EntitySpawnTemplate*> templates;

    AssetNode doc;
    std::string error;
    if (!loadAssetFile(path, doc, error))
    {
        Log::warning("Scene: prefab load failed: " + error);
        return templates;
    }

    const std::vector<const AssetNode*> prefabNodes = doc.findAll("Prefab");
    if (prefabNodes.empty())
        Log::warning("Scene: prefab '" + path + "' contained no Prefab declarations");

    templates.reserve(prefabNodes.size());
    for (const AssetNode* pn : prefabNodes)
        if (const EntitySpawnTemplate* tmpl = cachePrefabTemplate(pn->asString(), *pn))
            templates.push_back(tmpl);
    return templates;
}

const EntitySpawnTemplate* World::getOrBuildPrefabTemplate(const std::string& name)
{
    if (auto it = m_prefabTemplates.find(name); it != m_prefabTemplates.end())
        return it->second.get(); // cache hit: no asset file touched

    if (m_loadingPrefabs.contains(name))
    {
        Log::warning("Scene: prefab cycle detected at '" + name + "', skipping");
        return nullptr;
    }
    const std::string* prefabPath = Globals::assetRegistry.findPrefab(name);
    if (!prefabPath)
    {
        Log::warning("Scene: references unknown prefab '" + name + "', skipping");
        return nullptr;
    }

    // Read the file once, building every prefab it declares; the requested one is then cached.
    buildPrefabFileTemplates(*prefabPath);
    if (auto it = m_prefabTemplates.find(name); it != m_prefabTemplates.end())
        return it->second.get();

    Log::warning("Scene: prefab '" + name + "' not declared in '" + *prefabPath + "', skipping");
    return nullptr;
}

std::vector<EntityPtr> World::loadPrefab(const std::string& path, const Transform& base)
{
    std::vector<EntityPtr> roots;

    // Resolve the dropped file to the prefab names it declared (mapped at scan time, no file read),
    // then fetch each template cache-first: a previously loaded prefab spawns without touching its
    // .pre at all. A file the registry hasn't scanned (e.g. just saved) is parsed once as a fallback.
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(path, ec);
    const std::string fileName = (ec || relativePath.empty()) ? path : relativePath.string();

    std::vector<const EntitySpawnTemplate*> templates;
    if (const std::vector<std::string>* names = Globals::assetRegistry.findObjectsForFile(fileName))
    {
        templates.reserve(names->size());
        for (const std::string& name : *names)
            if (const EntitySpawnTemplate* tmpl = getOrBuildPrefabTemplate(name))
                templates.push_back(tmpl);
    }
    else
    {
        templates = buildPrefabFileTemplates(path);
    }
    if (templates.empty())
        return roots;

    // Offset the whole hierarchy so its first root lands where the prefab was dropped; each root keeps
    // its own authored rotation/scale, only its position shifts, preserving the authored relative layout.
    const glm::vec3 firstPos = templates[0]->defaultTransform.pos;
    for (const EntitySpawnTemplate* tmpl : templates)
    {
        const Transform& dt = tmpl->defaultTransform;
        const Transform rootBase(base.pos + (dt.pos - firstPos), dt.scale, dt.quat);
        roots.push_back(spawnFromTemplate(*tmpl, rootBase));
    }
    return roots;
}

std::vector<EntityPtr> World::spawnAssetFile(const std::string& path, const Transform& base)
{
    std::vector<EntityPtr> spawned;

    // Resolve the dropped file to the names it declared (mapped at scan time) — no re-reading —
    // then spawn each by name. Non-entity declarations (e.g. a bare ObjectContainer) have no
    // template and are skipped by spawn(). The drop payload is an absolute path; the registry
    // keys by path relative to its scan root (the Assets working dir), so relativize first.
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(path, ec);
    const std::string fileName = (ec || relativePath.empty()) ? path : relativePath.string();
    const std::vector<std::string>* names = Globals::assetRegistry.findObjectsForFile(fileName);
    if (!names)
    {
        Log::warning("Scene: dropped file '" + fileName + "' was not found in the asset registry");
        return spawned;
    }

    for (const std::string& name : *names)
        if (EntityPtr entity = spawn(name, base))
            spawned.push_back(std::move(entity));
    return spawned;
}