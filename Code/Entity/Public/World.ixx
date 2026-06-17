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

    // Scans the Assets tree so prefabs can be resolved by name. Prefab templates are built lazily on
    // first reference (and the ObjectContainers they use loaded then). Returns false if scanning fails.
    bool initialize();

    // Spawn the prefab registered under `name` (the reference used across .pre files, e.g. "sponza").
    // Returns the owning handle to the spawned entity, or a null EntityPtr if no prefab is registered
    // under that name. The caller owns the returned entity: let the handle drop to destroy it.
    EntityPtr spawn(const std::string& name, const Transform& base);

    // Spawn the single root a dropped ".pre" file declares — a prefab hierarchy — and return its owning
    // handle. The file resolves to its root via the registry's file map (no re-read;
    // a just-saved file falls back to a single parse). The root composes its authored transform onto
    // `base`; with overrideDefaultTransform (a viewport drop at the cursor) the authored position is
    // cancelled so it lands exactly at `base`. A prefab's children come along inside their root.
    EntityPtr spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform = true);

    // Discards every cached spawn template so the next spawn rebuilds from the (possibly just-overwritten)
    // .pre files — e.g. after re-saving a prefab in the editor. Already-spawned entities keep working:
    // their old templates are retired (kept alive) rather than freed, since entities hold raw pointers
    // into them. Call after writing a .pre to make the change take effect without a restart.
    void reloadPrefabs();

    // Returns the ObjectContainer registered under name, loading it on first use.
    ObjectContainer* getOrLoadContainer(const std::string& name);

    size_t getNumContainers() const { return m_containers.size(); }

private:

    ObjectContainer* loadContainer(const ObjectContainerDesc& desc);

    // Returns the cached spawn template for the prefab registered under `name`, building it from its
    // ".pre" file on a miss (which recursively resolves any prefabs it references). A cache hit touches
    // no asset file; any file is read at most once. Null if `name` names no registered prefab.
    std::shared_ptr<const EntitySpawnTemplate> getOrBuildPrefabTemplate(const std::string& name);

    // Builds and caches the prefab template `name` (loaded from `sourceFile`) from its already-parsed
    // root declaration node.
    std::shared_ptr<const EntitySpawnTemplate> cacheTemplate(const std::string& name, const std::string& sourceFile, const AssetNode& node);

    // Builds a one-off template from an inline "Entity" declaration node (a child defined fully inside
    // its parent's file). Not cached or named — owned by the parent's SceneComponent::SpawnInfo.
    std::shared_ptr<const EntitySpawnTemplate> buildInlineTemplate(const AssetNode& node);

    // Reads a ".pre" file once and builds+caches the template for the single root prefab it declares.
    // The fallback for a dropped file the registry hasn't scanned. Null if it declares no prefab.
    std::shared_ptr<const EntitySpawnTemplate> buildFileTemplate(const std::string& path);

    // Builds `tmpl` from a prefab/inline-entity declaration node: bakes placement/name, then the
    // parse-once SpawnInfo for each component the node declares (a RenderNode mesh, a Scene block of
    // children, or both). The single point where a declaration is turned into a template.
    void buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl);

    // Resolves a "RenderNode" component block to a cached RenderComponent::SpawnInfo (its container +
    // node + local transform), or null if the referenced ObjectContainer can't be loaded.
    std::shared_ptr<RenderComponent::SpawnInfo> buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName);

    // Resolves a "Scene" component block to a SceneComponent::SpawnInfo: its Enabled flag plus each
    // child's resolved template and placement (relative to the parent, taken as authored). An "Entity"
    // child builds an inline template; a "Prefab" child resolves the referenced prefab through the cache.
    std::shared_ptr<SceneComponent::SpawnInfo> buildSceneSpawnInfo(const AssetNode& sceneNode);

    std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
    std::unordered_map<std::string, std::shared_ptr<EntitySpawnTemplate>> m_templates; // prefab templates, keyed by name
    std::vector<std::shared_ptr<EntitySpawnTemplate>> m_retiredTemplates; // superseded by reloadPrefabs, kept alive for live entities
    std::unordered_set<std::string> m_buildingTemplates; // prefab names currently being built (cycle guard)
};
