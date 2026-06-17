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

    // Spawn every object a dropped asset file declares — entities and prefab hierarchies alike — and
    // return an owning handle per top-level root. The file resolves to its declarations via the
    // registry's file map (no re-read; a just-saved file falls back to a single parse). The first
    // declaration composes its full authored transform (position/rotation/scale) onto `base`. With
    // overrideDefaultTransform (a viewport drop at the cursor), the first declaration's authored position is
    // instead cancelled so it lands exactly at `base` and the rest keep their offset from it. A prefab's
    // children come along inside their root.
    std::vector<EntityPtr> spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform = true);

    // Returns the ObjectContainer registered under name, loading it on first use.
    ObjectContainer* getOrLoadContainer(const std::string& name);

    size_t getNumContainers() const { return m_containers.size(); }

private:

    ObjectContainer* loadContainer(const ObjectContainerDesc& desc);

    // Returns the spawn template for `name` — an entity (".ent") or a prefab (".pre"), built the same
    // way. A cache hit touches no asset file; a miss builds from the registry (a prefab read pulls in
    // every declaration in its file). Nested references resolve through here, so any file is read at
    // most once. Null if `name` resolves to nothing.
    const EntitySpawnTemplate* getOrBuildTemplate(const std::string& name);

    // Builds and caches a template from an already-parsed entity/prefab declaration node.
    const EntitySpawnTemplate* cacheTemplate(const std::string& name, const std::string& filePath, const AssetNode& node);

    // Reads an asset file once and builds+caches a template for every entity/prefab it declares,
    // returning them in declaration order. The fallback for a dropped file the registry hasn't scanned.
    std::vector<const EntitySpawnTemplate*> buildFileTemplates(const std::string& path);

    // Builds `tmpl` from an entity/prefab declaration node: bakes placement/name/sourceAsset, then the
    // parse-once SpawnInfo for each component the node declares (a RenderNode mesh, a Scene block of
    // children, or both). This is the single point where entities and prefabs are turned into templates.
    void buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl);

    // Resolves a "RenderNode" component block to a cached RenderComponent::SpawnInfo (its container +
    // node + local transform), or null if the referenced ObjectContainer can't be loaded.
    std::shared_ptr<RenderComponent::SpawnInfo> buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName);

    // Resolves a "Scene" component block to a SceneComponent::SpawnInfo: its Enabled flag plus each
    // child reference's resolved template and placement (stored relative to `origin`).
    std::shared_ptr<SceneComponent::SpawnInfo> buildSceneSpawnInfo(const AssetNode& sceneNode, const glm::vec3& origin);

    std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
    std::unordered_map<std::string, std::unique_ptr<EntitySpawnTemplate>> m_templates; // entity + prefab templates, keyed by name
    std::unordered_set<std::string> m_buildingTemplates; // names currently being built (cycle guard)
};
