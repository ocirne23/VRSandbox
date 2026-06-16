module Entity.World;

import Core;
import Core.glm;
import Core.Log;

import RendererVK;
import Entity;
import Entity.Component;
import Entity.Registry;
import File.ISceneData;
import File.AssetParser;

import Entity.AssetRegistry;
import Entity.ObjectDescription;

namespace Scene
{
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
        {
            const SpawnTemplate& tmpl = *it->second;
            EntityPtr entity = createEntity(tmpl.archetype, base);
            entity->name = name;
            entity->sourceAsset = name; // lets a saved prefab re-spawn this entity from its template
            entity->spawnTemplate = it->second.get();
            applySpawnInfos(tmpl, entity, base);
            return entity;
        }

        Log::warning("Scene: spawn() found no spawnable entity named '" + name + "'");
        return EntityPtr{};
    }

    // ---- spawn template registration (startup only) ------------------------------------------------
    // Resolving an entity (loading its container, looking up the node index, parsing transform
    // properties) and computing its archetype happens once, here. Only entities are spawnable;
    // ObjectContainers are loaded on demand as dependencies but never get a template of their own.

    void World::applySpawnInfos(const SpawnTemplate& tmpl, Entity* entity, const Transform& base) const
    {
        // spawnInfos is parallel to the set bits of archetype.typeBits (id order), so walk the bits
        // and the slots together; the slot's concrete type is implied by the bit it lines up with.
        size_t idx = 0;
        for (uint16 i = 0; i < MaxInlineComponentTypes; ++i)
        {
            if (!(tmpl.archetype.typeBits & (1 << i)))
                continue;
            if (const void* info = tmpl.spawnInfos[idx].get())
                spawnComponent(entity, EComponentID(i), info, base);
            ++idx;
        }
    }

    void World::registerTemplate(const EntityDesc& desc)
    {
        SpawnTemplate tmpl;
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
        m_spawnTemplates.emplace(desc.name, std::make_unique<SpawnTemplate>(std::move(tmpl)));
    }

    // ---- prefab (.pre) loading ---------------------------------------------------------------------

    static Transform readPrefabTransform(const AssetNode& node, const glm::vec3& delta)
    {
        Transform t;
        t.scale = 1.0f;
        if (const AssetNode* n = node.find("Position")) t.pos = n->asVec3();
        if (const AssetNode* n = node.find("Rotation")) t.quat = glm::quat(glm::radians(n->asVec3()));
        if (const AssetNode* n = node.find("Scale"))    t.scale = n->asFloat(0, 1.0f);
        t.pos += delta;
        return t;
    }

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

    void World::instantiateSceneChildren(const AssetNode& sceneNode, const glm::vec3& delta, Entity* parent)
    {
        // Preserve authoring order: an "Entity" is a ".ent" reference, a "Prefab" is a nested prefab.
        for (const AssetNode& child : sceneNode.children)
        {
            if (keyIs(child, "Entity"))
                instantiatePrefabNode(child, delta, parent);
            else if (keyIs(child, "Prefab"))
                instantiateNestedPrefab(child, delta, parent);
        }
    }

    EntityPtr World::instantiatePrefabDef(const AssetNode& def, const glm::vec3& delta, Entity* parent)
    {
        // A prefab is intrinsically a Scene container (not a ".ent" reference); its Component blocks
        // carry only variable overrides, and its Scene block holds the child references.
        const Transform t = readPrefabTransform(def, delta);
        EntityPtr entity = createEntity(makeEntityArchetype(1 << EComponentID_Scene), t);
        entity->name = def.asString();
        entity->sourceAsset = def.asString(); // prefab id, so it round-trips as a nested "Prefab" ref
        if (const AssetNode* n = def.find("Name")) entity->name = n->asString();

        for (const AssetNode* comp : def.findAll("Component"))
            if (int id = componentIdFromName(comp->asString()); id >= 0 && (entity->typeBits & (1 << id)))
                deserializeComponent(entity, EComponentID(id), *comp);

        reparentEntity(entity, parent); // null resolves to the World root

        if (const AssetNode* sceneNode = findComponentNode(def, "Scene"))
            instantiateSceneChildren(*sceneNode, delta, entity);

        return entity;
    }

    EntityPtr World::instantiateNestedPrefab(const AssetNode& ref, const glm::vec3& delta, Entity* parent)
    {
        const std::string name = ref.asString();

        if (m_loadingPrefabs.contains(name))
        {
            Log::warning("Scene: prefab cycle detected at '" + name + "', skipping");
            return EntityPtr{};
        }
        const std::string* prefabPath = Globals::assetRegistry.findPrefab(name);
        if (!prefabPath)
        {
            Log::warning("Scene: prefab references unknown prefab '" + name + "', skipping");
            return EntityPtr{};
        }

        AssetNode doc;
        std::string error;
        if (!loadAssetFile(*prefabPath, doc, error))
        {
            Log::warning("Scene: nested prefab load failed: " + error);
            return EntityPtr{};
        }
        const AssetNode* def = doc.find("Prefab");
        if (!def)
        {
            Log::warning("Scene: nested prefab '" + name + "' has no Prefab declaration, skipping");
            return EntityPtr{};
        }

        // The reference node places the instance; its children sit relative to that placement (the
        // definition's own root position is its local origin, so subtract it).
        const Transform place = readPrefabTransform(ref, delta);
        glm::vec3 defRootPos(0.0f);
        if (const AssetNode* p = def->find("Position")) defRootPos = p->asVec3();
        const glm::vec3 childDelta = place.pos - defRootPos;

        EntityPtr entity = createEntity(makeEntityArchetype(1 << EComponentID_Scene), place);
        entity->name = name;
        entity->sourceAsset = name; // round-trips as a nested "Prefab" ref
        if (const AssetNode* n = def->find("Name")) entity->name = n->asString();
        if (const AssetNode* n = ref.find("Name"))  entity->name = n->asString();

        // Component variable overrides: the definition's, then the reference node's on top.
        for (const AssetNode* comp : def->findAll("Component"))
            if (int id = componentIdFromName(comp->asString()); id >= 0 && (entity->typeBits & (1 << id)))
                deserializeComponent(entity, EComponentID(id), *comp);
        for (const AssetNode* comp : ref.findAll("Component"))
            if (int id = componentIdFromName(comp->asString()); id >= 0 && (entity->typeBits & (1 << id)))
                deserializeComponent(entity, EComponentID(id), *comp);

        reparentEntity(entity, parent);

        m_loadingPrefabs.insert(name);
        if (const AssetNode* sceneNode = findComponentNode(*def, "Scene"))
            instantiateSceneChildren(*sceneNode, childDelta, entity);
        m_loadingPrefabs.erase(name);

        return entity;
    }

    EntityPtr World::instantiatePrefabNode(const AssetNode& node, const glm::vec3& delta, Entity* parent)
    {
        const Transform t = readPrefabTransform(node, delta);
        const std::string token = node.asString(); // ".ent" template this entity references

        // Every prefab entity references a registered ".ent" template; the component SET always comes
        // from the template. Unknown references are warned about and skipped.
        auto it = m_spawnTemplates.find(token);
        if (it == m_spawnTemplates.end())
        {
            Log::warning("Scene: prefab references unknown entity '" + token + "', skipping");
            return EntityPtr{};
        }
        const SpawnTemplate* tmpl = it->second.get();

        EntityPtr entity = createEntity(tmpl->archetype, t);
        entity->name = token;
        entity->sourceAsset = token; // re-resolves this reference on the next load
        entity->spawnTemplate = tmpl;
        applySpawnInfos(*tmpl, entity, t);

        if (const AssetNode* n = node.find("Name")) entity->name = n->asString();

        // Apply per-component variable overrides authored in the editor. Each component's serialize/
        // deserialize only touches listed variables, so this patches tweaks on top of the template.
        for (const AssetNode* comp : node.findAll("Component"))
            if (int id = componentIdFromName(comp->asString()); id >= 0 && (entity->typeBits & (1 << id)))
                deserializeComponent(entity, EComponentID(id), *comp);

        // Hook into the scene graph / ownership: scene entities go under the World root, loose roots
        // stay owned by the returned handle (the app keeps them alive).
        if (parent)
            reparentEntity(entity, parent);
        else if (hasComponent<SceneComponent>(entity))
            reparentEntity(entity, nullptr); // resolves to the World root

        // Child references are nested directly inside the Scene component block.
        if (const AssetNode* sceneNode = findComponentNode(node, "Scene"))
            instantiateSceneChildren(*sceneNode, delta, entity);

        return entity;
    }

    std::vector<EntityPtr> World::loadPrefab(const std::string& path, const Transform& base)
    {
        std::vector<EntityPtr> roots;

        AssetNode doc;
        std::string error;
        if (!loadAssetFile(path, doc, error))
        {
            Log::warning("Scene: prefab load failed: " + error);
            return roots;
        }

        const std::vector<const AssetNode*> prefabNodes = doc.findAll("Prefab");
        if (prefabNodes.empty())
        {
            Log::warning("Scene: prefab '" + path + "' contained no Prefab declarations");
            return roots;
        }

        // Offset the whole hierarchy so its first root lands where the prefab was dropped.
        glm::vec3 firstPos(0.0f);
        if (const AssetNode* p = prefabNodes[0]->find("Position")) firstPos = p->asVec3();
        const glm::vec3 delta = base.pos - firstPos;

        for (const AssetNode* pn : prefabNodes)
        {
            const std::string name = pn->asString();
            m_loadingPrefabs.insert(name); // guard self/cyclic nested references
            roots.push_back(instantiatePrefabDef(*pn, delta, nullptr));
            m_loadingPrefabs.erase(name);
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
}
