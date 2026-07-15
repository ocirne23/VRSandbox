export module Entity:World;

import Core;
import Core.glm;
import Core.Transform;
import Core.Camera;
import Core.Rect;

import RendererVK;
import Animation;
import Physics;
import Audio;
import :Entity;
import :Component;

import File;
import Spatial;
import :AnimationDescription;

// Flattened collision geometry for one spawnable node (positions + triangle indices, node
// transforms applied), built transiently from a CollisionSource for hull/mesh physics shapes.
export struct PhysicsGeometry
{
    std::vector<glm::vec3> vertices;
    std::vector<uint32> indices;
};

// Stripped CPU snapshot of a container's scene: mesh positions/indices plus the node tree. Captured
// from the same ISceneData the render container is built from (loadContainer), so the source file is
// imported once; per-node collision geometry is then derived from this without touching the file.
//
// Meshes/nodes named "Col_*" are artist-authored collision proxies: physics collides against them
// (at their own placement) INSTEAD of the same-named render mesh ("Col_Wall" replaces "Wall"), and
// the renderer never draws them. Meshes without a proxy collide as themselves.
export struct CollisionSource
{
    struct Mesh
    {
        std::string name;
        std::vector<glm::vec3> vertices;
        std::vector<uint32> indices;
    };
    struct Node
    {
        std::string name;
        glm::mat4 localTransform = glm::mat4(1.0f);
        std::vector<uint32> meshIndices;
        std::vector<Node> children;
    };
    std::vector<Mesh> meshes;
    Node root;
    std::unordered_set<std::string> proxiedNames; // names that have a "Col_" proxy (prefix stripped)
};

export class World final
{
public:

    bool initialize();

    EntityPtr spawn(const std::string& name, const Transform& base);

    EntityPtr spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform = true);

    EntityPtr createEmptyEntity(const std::string& name);

    // --- Root entity ownership (the world controls lifetimes; was a main.cpp local). Roots hold
    // the owning refs for every entity not parented under another; the App's update loop iterates
    // them and its spawn controls append. clearRootEntities() must run before main returns:
    // World is a global, and globals in different libraries have no defined destruction order -
    // entities must die while the renderer/physics globals are certainly still alive.
    void addRootEntity(EntityPtr entity) { if (entity) m_rootEntities.push_back(std::move(entity)); }
    const std::vector<EntityPtr>& rootEntities() const { return m_rootEntities; } // read-only: all mutation goes through the World API
    void clearRootEntities() { m_rootEntities.clear(); }

    // Applies one queued EntityChange (from the UI panels or script events) - all root-list and
    // entity-lifetime mutations funnel through here. camera + viewportRect resolve viewport-drop
    // spawns to world positions.
    void handleEntityChange(EntityChange& change, const Camera& camera, const Rect& viewportRect);

    // Editor notifications emitted while applying changes; the App wires these to the UI (the
    // dependency points UI -> Entity, so World cannot call the UI directly).
    void setOnPrefabOpened(std::function<void(const EntityPtr&, const std::string&)> callback) { m_onPrefabOpened = std::move(callback); }
    void setOnEntityRespawned(std::function<void(const EntityPtr&, const EntityPtr&)> callback) { m_onEntityRespawned = std::move(callback); }

    void reloadPrefabs();

    void invalidatePrefab(const std::string& name);

    // captureCollisionSource keeps a CPU snapshot of the scene geometry alongside the container, for
    // templates whose physics shape (Hull/Mesh) needs it — avoids re-importing the source file.
    ObjectContainer* getOrLoadContainer(const std::string& name, bool captureCollisionSource = false);

    size_t getNumContainers() const { return m_containers.size(); }

    // Entity::create() stores a raw, non-owning pointer to its template (entities normally rely on a
    // stable owner — World's own template caches — to keep it alive). Ad-hoc templates assembled outside
    // World (the Entity Editor's respawn-on-edit flow) have no such owner, so they're kept alive here
    // indefinitely instead (editing sessions create at most a few hundred of these — negligible memory).
    void keepTemplateAlive(std::shared_ptr<const EntitySpawnTemplate> tmpl) { m_editorTemplates.push_back(std::move(tmpl)); }

    // Component SpawnInfo builders, public so editor tooling (the Entity Editor) can resolve a single
    // component's spawn recipe from a small ad-hoc AssetNode fragment — the same inputs/outputs a normal
    // .pre load would produce — without going through a full prefab file. Order matters when used
    // together: build Render first (captureCollisionSource=true if a Hull/Mesh Physics shape is also
    // being built) so Physics can derive its geometry from the resulting container.
    std::shared_ptr<RenderComponent::SpawnInfo> buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName, bool captureCollisionSource = false);

    // Takes the sibling render container by name (resolved internally via getOrLoadContainer) rather than
    // a raw ObjectContainer* — passing that pointer across the module boundary tripped an MSVC C++20
    // modules cross-module mangling bug (unresolved external at link time) that a plain string sidesteps.
    std::shared_ptr<AnimatorComponent::SpawnInfo> buildAnimatorSpawnInfo(const AssetNode& animatorNode, const std::string& siblingContainerName, const std::string& ownerName);

    std::shared_ptr<PhysicsComponent::SpawnInfo> buildPhysicsSpawnInfo(const AssetNode& physicsNode,
        const std::string& containerName, const std::string& nodePath, const std::string& ownerName);

    std::shared_ptr<AudioComponent::SpawnInfo> buildAudioSpawnInfo(const AssetNode& audioNode, const std::string& ownerName);

private:

    ObjectContainer* loadContainer(const ObjectContainerDesc& desc, bool captureCollisionSource);

    // Builds (or returns a cached) clip library for an animator, retargeted against `skel`. Cached by
    // skeleton + animator name so a source FBX is imported once, not per spawned entity.
    const AnimationSet* getOrBuildClipSet(const Skeleton* skel, const AnimatorDesc& desc);

    std::shared_ptr<const EntitySpawnTemplate> getOrBuildPrefabTemplate(const std::string& name);

    std::shared_ptr<const EntitySpawnTemplate> cacheTemplate(const std::string& name, const std::string& sourceFile, const AssetNode& node);

    std::shared_ptr<const EntitySpawnTemplate> buildInlineTemplate(const AssetNode& node);

    std::shared_ptr<const EntitySpawnTemplate> buildFileTemplate(const std::string& path);

    void buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl);

    std::shared_ptr<SceneComponent::SpawnInfo> buildSceneSpawnInfo(const AssetNode& sceneNode);

    // Audio buffer for a sound file, shared between every entity referencing the same path. A failed
    // load is cached too (as an invalid buffer) so a bad path doesn't retry + re-log every spawn.
    std::shared_ptr<AudioBuffer> getOrLoadAudioBuffer(const std::string& path);

    // Collision source for a container: normally captured during loadContainer; falls back to a
    // one-time re-import if the container was loaded before physics asked (then cached).
    std::shared_ptr<const CollisionSource> getOrLoadCollisionSource(const std::string& containerName);
    std::shared_ptr<PhysicsMesh> getOrBuildCollisionMesh(const std::string& containerName, const std::string& nodePath);

    std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
    std::unordered_map<std::string, std::shared_ptr<const CollisionSource>> m_collisionSources; // key: container name
    std::unordered_map<std::string, std::shared_ptr<PhysicsMesh>> m_collisionMeshes;            // key: container|node
    std::unordered_map<std::string, std::shared_ptr<const OccluderData>> m_occluderData;        // key: container|node
    std::unordered_map<std::string, std::unique_ptr<AnimationSet>> m_clipSets; // key: skeleton ptr + animator name
    std::unordered_map<std::string, std::shared_ptr<AudioBuffer>> m_audioBuffers; // key: sound file path
    std::unordered_map<std::string, std::shared_ptr<EntitySpawnTemplate>> m_templates; // prefab templates, keyed by name
    std::vector<std::shared_ptr<EntitySpawnTemplate>> m_retiredTemplates; // superseded by reloadPrefabs, kept alive for live entities
    std::unordered_set<std::string> m_buildingTemplates; // prefab names currently being built (cycle guard)
    std::shared_ptr<EntitySpawnTemplate> m_emptyTemplate; // blank Scene-only template for editable (non-prefab) entities
    std::vector<std::shared_ptr<const EntitySpawnTemplate>> m_editorTemplates; // ad-hoc templates kept alive via keepTemplateAlive()
    std::vector<EntityPtr> m_rootEntities;
    std::function<void(const EntityPtr&, const std::string&)> m_onPrefabOpened;
    std::function<void(const EntityPtr&, const EntityPtr&)> m_onEntityRespawned;
};

export namespace Globals
{
    World world;
}
