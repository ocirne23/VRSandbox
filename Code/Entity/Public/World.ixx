export module Entity.World;

import Core;
import Core.glm;
import Core.Transform;

import RendererVK;
import Entity;
import Entity.Component;

import File.AssetParser;
import Entity.ObjectDescription;

// Owns the loaded ObjectContainers (immovable GPU data, kept alive and reused across spawns) and
// resolves name references through the asset registry. Spawning creates a real Entity (via
// createEntity) with a RenderNode living inline in its RenderComponent, and hands the owning
// EntityPtr back to the caller. The world does NOT retain entity references itself — gameplay code
// is responsible for keeping the returned handles alive; an entity is freed when its last handle
// drops. ObjectContainers, however, are retained (and reused) for as long as the world lives.
export class World final
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

    ObjectContainer* loadContainer(const ObjectContainerDesc& desc);
    void registerTemplate(const EntityDesc& desc);

    // Returns the spawn template for prefab `name`: a cache hit touches no asset file, a miss reads
    // the registered .pre once (building every prefab it declares). Nested "Prefab <name>" references
    // resolve through here, so a prefab referenced many times is only read once. Null if unresolved.
    const EntitySpawnTemplate* getOrBuildPrefabTemplate(const std::string& name);

    // Reads a .pre file once and builds+caches a template for every "Prefab" it declares, returning
    // them in declaration order. The fallback for a dropped file the registry hasn't scanned.
    std::vector<const EntitySpawnTemplate*> buildPrefabFileTemplates(const std::string& path);

    // Builds and caches a prefab template from an already-parsed "Prefab" definition node.
    const EntitySpawnTemplate* cachePrefabTemplate(const std::string& name, const AssetNode& def);

    // Fills `tmpl` (a Scene-archetype container) from a parsed "Prefab" definition: its Scene
    // component's Enabled flag and child references become a SceneComponent::SpawnInfo.
    void buildPrefabTemplate(const AssetNode& def, EntitySpawnTemplate& tmpl);

    // Resolves the "Entity"/"Prefab" child references inside a Scene component block to their spawn
    // templates and placements, appending them to `out`. `defOrigin` is the prefab root's authored
    // position, so child positions are stored relative to it.
    void buildSceneChildren(const AssetNode& sceneNode, const glm::vec3& defOrigin, SceneComponent::SpawnInfo& out);

    std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
    std::unordered_map<std::string, std::unique_ptr<EntitySpawnTemplate>> m_spawnTemplates;
    std::unordered_map<std::string, std::unique_ptr<EntitySpawnTemplate>> m_prefabTemplates; // built-once prefab templates, keyed by prefab name
    std::unordered_set<std::string> m_loadingPrefabs; // names currently being built (cycle guard)
};
