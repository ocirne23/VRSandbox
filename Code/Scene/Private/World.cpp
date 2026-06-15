module Scene.World;

import Core;
import Core.glm;
import Core.Log;

import RendererVK;
import Entity;
import Entity.Component;
import File.ISceneData;

import Scene.AssetRegistry;
import Scene.ObjectDescription;

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
            const SpawnTemplate& tmpl = it->second;
            EntityPtr entity = createEntity(tmpl.archetype, base);
            if (tmpl.container)
            {
                const glm::quat rot = glm::normalize(base.quat * tmpl.localTransform.quat);
                const Transform transform(base.pos + tmpl.localTransform.pos, base.scale * tmpl.localTransform.scale, rot);
                getComponent<RenderComponent>(entity)->node = tmpl.container->spawnNodeForIdx(tmpl.nodeIdx, transform);
            }
            return entity;
        }

        Log::warning("Scene: spawn() found no spawnable entity named '" + name + "'");
        return EntityPtr{};
    }

    // ---- spawn template registration (startup only) ------------------------------------------------
    // Resolving an entity (loading its container, looking up the node index, parsing transform
    // properties) and computing its archetype happens once, here. Only entities are spawnable;
    // ObjectContainers are loaded on demand as dependencies but never get a template of their own.

    void World::registerTemplate(const EntityDesc& desc)
    {
        SpawnTemplate tmpl;
        uint16 typeBits = 0;

        // Only the RenderNode component maps to an inline component today; the Render bit (and inline
        // storage) is added only if its container resolves.
        if (const ComponentDesc* renderDesc = desc.findComponent("RenderNode"))
        {
            if (ObjectContainer* container = getOrLoadContainer(renderDesc->property("ObjectContainer")))
            {
                typeBits |= uint16(1 << EComponentID_Render);
                tmpl.container = container;

                const std::string node = renderDesc->property("Node");
                if (node.empty() || node == "ROOT")
                    tmpl.nodeIdx = NodeSpawnIdx_ROOT;
                else if (NodeSpawnIdx idx = container->getSpawnIdxForPath(node); idx != NodeSpawnIdx_INVALID)
                    tmpl.nodeIdx = idx;
                else
                    Log::warning("Scene: entity '" + desc.name + "' references unknown node '" + node + "', using ROOT");

                const glm::quat rot = glm::normalize(glm::quat(glm::radians(renderDesc->vec3Property("Rotation"))));
                tmpl.localTransform = Transform(renderDesc->vec3Property("Position"), renderDesc->floatProperty("Scale", 1.0f), rot);
            }
        }

        tmpl.archetype = makeEntityArchetype(typeBits);
        m_spawnTemplates.emplace(desc.name, tmpl); // keep-first (an entity may share a name with its container)
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
