export module Entity:World;

import Core;
import Core.glm;
import Core.Transform;

import RendererVK;
import Animation;
import Physics;
import :Entity;
import :Component;

import File;
import :ObjectDescription;
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
export struct CollisionSource
{
    struct Mesh
    {
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
};

export class World final
{
public:

    bool initialize();

    EntityPtr spawn(const std::string& name, const Transform& base);

    EntityPtr spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform = true);

    EntityPtr createEmptyEntity(const std::string& name);

    void reloadPrefabs();

    void invalidatePrefab(const std::string& name);

    // captureCollisionSource keeps a CPU snapshot of the scene geometry alongside the container, for
    // templates whose physics shape (Hull/Mesh) needs it — avoids re-importing the source file.
    ObjectContainer* getOrLoadContainer(const std::string& name, bool captureCollisionSource = false);

    size_t getNumContainers() const { return m_containers.size(); }

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

    std::shared_ptr<RenderComponent::SpawnInfo> buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName, bool captureCollisionSource = false);

    std::shared_ptr<AnimatorComponent::SpawnInfo> buildAnimatorSpawnInfo(const AssetNode& animatorNode, ObjectContainer* siblingContainer, const std::string& ownerName);

    std::shared_ptr<SceneComponent::SpawnInfo> buildSceneSpawnInfo(const AssetNode& sceneNode);

    std::shared_ptr<PhysicsComponent::SpawnInfo> buildPhysicsSpawnInfo(const AssetNode& physicsNode,
        const std::string& containerName, const std::string& nodePath, const std::string& ownerName);

    // Collision source for a container: normally captured during loadContainer; falls back to a
    // one-time re-import if the container was loaded before physics asked (then cached).
    std::shared_ptr<const CollisionSource> getOrLoadCollisionSource(const std::string& containerName);
    std::shared_ptr<PhysicsMesh> getOrBuildCollisionMesh(const std::string& containerName, const std::string& nodePath);

    std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
    std::unordered_map<std::string, std::shared_ptr<const CollisionSource>> m_collisionSources; // key: container name
    std::unordered_map<std::string, std::shared_ptr<PhysicsMesh>> m_collisionMeshes;            // key: container|node
    std::unordered_map<std::string, std::unique_ptr<AnimationSet>> m_clipSets; // key: skeleton ptr + animator name
    std::unordered_map<std::string, std::shared_ptr<EntitySpawnTemplate>> m_templates; // prefab templates, keyed by name
    std::vector<std::shared_ptr<EntitySpawnTemplate>> m_retiredTemplates; // superseded by reloadPrefabs, kept alive for live entities
    std::unordered_set<std::string> m_buildingTemplates; // prefab names currently being built (cycle guard)
    std::shared_ptr<EntitySpawnTemplate> m_emptyTemplate; // blank Scene-only template for editable (non-prefab) entities
};

export namespace Globals
{
    World world;
}
