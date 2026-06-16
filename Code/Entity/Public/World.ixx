export module Entity.World;

import Core;
import Core.glm;
import Core.Transform;

import RendererVK;
import Entity;

import File.AssetParser;
import Entity.ObjectDescription;

export namespace Scene
{
    // Owns the loaded ObjectContainers (immovable GPU data, kept alive and reused across spawns) and
    // resolves name references through the asset registry. Spawning creates a real Entity (via
    // createEntity) with a RenderNode living inline in its RenderComponent, and hands the owning
    // EntityPtr back to the caller. The world does NOT retain entity references itself — gameplay code
    // is responsible for keeping the returned handles alive; an entity is freed when its last handle
    // drops. ObjectContainers, however, are retained (and reused) for as long as the world lives.
    class World final
    {
    public:

        // Scans the Assets tree and builds a spawn template for every registered object up front, so
        // spawning never has to interpret an asset file. Returns false if scanning fails.
        bool initialize();

        // Spawn a single registered object by its declared name (the reference used across asset
        // files, e.g. "sponza"). One template lookup, no branching on object kind. Returns the owning
        // handle to the spawned entity, or a null EntityPtr if no object is registered under that name.
        // The caller owns the returned entity: let the handle drop to destroy it.
        EntityPtr spawn(const std::string& name, const Transform& base);

        // Spawn every object declared in a dropped asset file, anchored at base. The file path is
        // resolved to its declaration names via the registry's file map (no re-reading), then each is
        // spawned by name. Returns a handle per spawned entity; empty if the file isn't known.
        std::vector<EntityPtr> spawnAssetFile(const std::string& path, const Transform& base);

        // Loads a prefab (.pre) hierarchy saved from the editor and instantiates it, re-spawning each
        // entity's source ".ent" (mesh/RenderNode) and rebuilding the parent/child structure. The whole
        // hierarchy is offset so its first root lands at `base`. Returns the owning handle(s) to the
        // top-level entit(ies); scene-component roots are also registered with the EntityRegistry.
        std::vector<EntityPtr> loadPrefab(const std::string& path, const Transform& base);

        // Returns the ObjectContainer registered under name, loading it on first use.
        ObjectContainer* getOrLoadContainer(const std::string& name);

        size_t getNumContainers() const { return m_containers.size(); }

    private:

        // Everything needed to spawn a named object without touching its asset desc again: the
        // entity archetype (alloc size + component mask), the resolved container + node, and the
        // baked local transform. Built once per name and reused on every spawn.
        struct SpawnTemplate
        {
            ObjectContainer* container = nullptr;          // null = entity carries no RenderComponent
            NodeSpawnIdx nodeIdx = NodeSpawnIdx_ROOT;
            EntityArchetype archetype;                     // alloc size + component mask, computed once
            Transform localTransform;                      // applied on top of the spawn's base transform
            std::string filePath;                          // ".ent" file this template was built from
        };

        ObjectContainer* loadContainer(const ObjectContainerDesc& desc);
        void registerTemplate(const EntityDesc& desc);

        // Recursively instantiates one prefab "Entity" node (and its children), offsetting positions by
        // `delta` and parenting under `parent` (nullptr = a top-level root).
        EntityPtr instantiatePrefabNode(const AssetNode& node, const glm::vec3& delta, Entity* parent);

        std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
        std::unordered_map<std::string, std::unique_ptr<SpawnTemplate>> m_spawnTemplates;
    };
}
