export module Entity.World;

import Core;
import Core.glm;
import Core.Transform;

import RendererVK;
import Entity;
import Entity.Component;

import File.AssetParser;
import Entity.ObjectDescription;

export class World final
{
public:

    bool initialize();

    EntityPtr spawn(const std::string& name, const Transform& base);

    EntityPtr spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform = true);

    EntityPtr createEmptyEntity(const std::string& name);

    void reloadPrefabs();

    ObjectContainer* getOrLoadContainer(const std::string& name);

    size_t getNumContainers() const { return m_containers.size(); }

private:

    ObjectContainer* loadContainer(const ObjectContainerDesc& desc);

    std::shared_ptr<const EntitySpawnTemplate> getOrBuildPrefabTemplate(const std::string& name);

    std::shared_ptr<const EntitySpawnTemplate> cacheTemplate(const std::string& name, const std::string& sourceFile, const AssetNode& node);

    std::shared_ptr<const EntitySpawnTemplate> buildInlineTemplate(const AssetNode& node);

    std::shared_ptr<const EntitySpawnTemplate> buildFileTemplate(const std::string& path);

    void buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl);

    std::shared_ptr<RenderComponent::SpawnInfo> buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName);

    std::shared_ptr<SceneComponent::SpawnInfo> buildSceneSpawnInfo(const AssetNode& sceneNode);

    std::unordered_map<std::string, std::unique_ptr<ObjectContainer>> m_containers;
    std::unordered_map<std::string, std::shared_ptr<EntitySpawnTemplate>> m_templates; // prefab templates, keyed by name
    std::vector<std::shared_ptr<EntitySpawnTemplate>> m_retiredTemplates; // superseded by reloadPrefabs, kept alive for live entities
    std::unordered_set<std::string> m_buildingTemplates; // prefab names currently being built (cycle guard)
    std::shared_ptr<EntitySpawnTemplate> m_emptyTemplate; // blank Scene-only template for editable (non-prefab) entities
};
