module Entity;

import Core;
import Core.glm;
import Core.Log;

import RendererVK;
import :Entity;
import :Component;
import File;

import :AssetRegistry;
import :ObjectDescription;
import :AnimationDescription;
import Animation;
import Physics;
import Audio;

bool World::initialize()
{
    Globals::assetRegistry.scanDirectory();
    return true;
}

static RendererVKLayout::EPipelineIndex parsePipeline(const std::string& name)
{
    using P = RendererVKLayout::EPipelineIndex;
    if (name == "LitTransparent")   return P::LitTransparent;
    if (name == "UnlitOpaque")      return P::UnlitOpaque;
    if (name == "UnlitTransparent") return P::UnlitTransparent;
    if (name == "Sky")              return P::Sky;
    if (name == "WireframeTransparent") return P::WireframeTransparent;
    if (name == "GizmoUI")          return P::GizmoUI;
    if (name == "GizmoWorld")       return P::GizmoWorld;
    return P::LitOpaque;
}

// Artist-authored collision proxies: "Col_Wall" collides in place of "Wall" and is never rendered
// (the renderer applies the same prefix rule in ObjectContainer::initializeNodes).
static constexpr std::string_view COLLISION_MESH_PREFIX = "Col_";

static bool isCollisionName(std::string_view name) { return name.starts_with(COLLISION_MESH_PREFIX); }

static glm::mat4 nodeLocalTransform(const INodeData& node)
{
    glm::vec3 pos, scale;
    glm::quat rot;
    node.getTransform(pos, scale, rot);
    // The renderer flattens node scale to uniform scale.x (ObjectContainer::initializeNodes); collision
    // must match what is drawn, not the source data.
    return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), glm::vec3(scale.x));
}

static void buildCollisionSourceNode(const INodeData& node, CollisionSource::Node& out)
{
    out.name = node.getName();
    out.localTransform = nodeLocalTransform(node);
    for (uint32 m = 0; m < node.getNumMeshes(); ++m)
        out.meshIndices.push_back(node.getMeshIndex(m));
    out.children.resize(node.getNumChildren());
    for (uint32 c = 0; c < node.getNumChildren(); ++c)
        buildCollisionSourceNode(*node.getChild(c), out.children[c]);
}

static void gatherProxiedNames(const CollisionSource::Node& node, std::unordered_set<std::string>& outNames)
{
    if (isCollisionName(node.name))
        outNames.insert(node.name.substr(COLLISION_MESH_PREFIX.size()));
    for (const CollisionSource::Node& child : node.children)
        gatherProxiedNames(child, outNames);
}

// Snapshots the collision-relevant parts of a loaded scene (mesh positions/indices + node tree), so
// hull/mesh physics shapes never need the source file again.
static std::shared_ptr<const CollisionSource> buildCollisionSource(const ISceneData& sceneData)
{
    auto source = std::make_shared<CollisionSource>();
    source->meshes.resize(sceneData.getNumMeshes());
    for (uint32 i = 0; i < sceneData.getNumMeshes(); ++i)
    {
        const IMeshData* mesh = sceneData.getMesh(i);
        if (!mesh)
            continue;
        CollisionSource::Mesh& outMesh = source->meshes[i];
        outMesh.name = mesh->getName();
        outMesh.vertices.assign(mesh->getVertices(), mesh->getVertices() + mesh->getNumVertices());
        outMesh.indices.assign(mesh->getIndices(), mesh->getIndices() + mesh->getNumIndices());
        if (isCollisionName(outMesh.name))
            source->proxiedNames.insert(outMesh.name.substr(COLLISION_MESH_PREFIX.size()));
    }
    buildCollisionSourceNode(sceneData.getRootNode(), source->root);
    gatherProxiedNames(source->root, source->proxiedNames);
    return source;
}

ObjectContainer* World::loadContainer(const ObjectContainerDesc& desc, bool captureCollisionSource)
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

    if (captureCollisionSource && !m_collisionSources.contains(desc.name))
        m_collisionSources.emplace(desc.name, buildCollisionSource(*sceneData));

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

ObjectContainer* World::getOrLoadContainer(const std::string& name, bool captureCollisionSource)
{
    if (auto it = m_containers.find(name); it != m_containers.end())
        return it->second.get(); // already loaded; a late collision request falls back to getOrLoadCollisionSource
    if (const ObjectContainerDesc* desc = Globals::assetRegistry.findObjectContainer(name))
        return loadContainer(*desc, captureCollisionSource);
    Log::warning("Scene: unknown ObjectContainer reference '" + name + "'");
    return nullptr;
}

EntityPtr World::spawn(const std::string& name, const Transform& base)
{
    if (std::shared_ptr<const EntitySpawnTemplate> tmpl = getOrBuildPrefabTemplate(name))
        return Entity::create(*tmpl, base);
    return EntityPtr{};
}

void World::reloadPrefabs()
{
    for (auto& [name, tmpl] : m_templates)
        m_retiredTemplates.push_back(std::move(tmpl));
    m_templates.clear();
}

void World::invalidatePrefab(const std::string& name)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
    {
        m_retiredTemplates.push_back(std::move(it->second)); // kept alive for live entities
        m_templates.erase(it);
    }
}

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

static Transform readNodeTransform(const AssetNode& node)
{
    Transform t;
    t.scale = 1.0f;
    if (const AssetNode* n = node.find("Position")) t.pos = n->asVec3();
    if (const AssetNode* n = node.find("Rotation")) t.quat = glm::quat(glm::radians(n->asVec3()));
    if (const AssetNode* n = node.find("Scale"))    t.scale = n->asFloat(0, 1.0f);
    return t;
}

std::shared_ptr<RenderComponent::SpawnInfo> World::buildRenderSpawnInfo(const AssetNode& renderNode, const std::string& ownerName, bool captureCollisionSource)
{
    const AssetNode* containerNode = renderNode.find("ObjectContainer");
    if (!containerNode)
        return nullptr;
    const std::string containerName = containerNode->asString();
    const std::string nodePath = renderNode.find("Node") ? renderNode.find("Node")->asString() : std::string();

    ObjectContainer* container = getOrLoadContainer(containerName, captureCollisionSource);
    if (!container)
        return nullptr;

    // Type defaults from the container itself (skinned iff its source scene had a skeleton) but can be
    // overridden explicitly — `Type StaticMesh` / `Type SkinnedMesh`, with an optional nested `Rig` token.
    bool skinned = container->isSkinned();
    std::string rigType;
    if (const AssetNode* typeNode = renderNode.find("Type"))
    {
        skinned = typeNode->asString() == "SkinnedMesh";
        if (const AssetNode* rigNode = typeNode->find("Rig"))
            rigType = rigNode->asString();
    }

    auto info = std::make_shared<RenderComponent::SpawnInfo>();
    info->container = container;
    info->containerName = containerName; // kept so an inline entity re-serializes its mesh
    info->skinned = skinned;
    info->rigType = rigType;

    if (nodePath.empty() || nodePath == "ROOT")
        info->nodeIdx = NodeSpawnIdx_ROOT;
    else if (NodeSpawnIdx idx = container->getSpawnIdxForPath(nodePath); idx != NodeSpawnIdx_INVALID)
        info->nodeIdx = idx;
    else
        Log::warning("Scene: entity '" + ownerName + "' references unknown node '" + nodePath + "', using ROOT");
    info->nodePath = nodePath.empty() ? "ROOT" : nodePath;
    info->localTransform = readNodeTransform(renderNode); // mesh offset within the entity
    return info;
}

void writeRenderSpawnInfo(const RenderComponent::SpawnInfo& info, AssetNode& out)
{
    if (!info.container)
        return;
    out.set("ObjectContainer", info.containerName);
    out.set("Node", info.nodePath);
    AssetNode& typeNode = out.addChild("Type");
    typeNode.values.emplace_back(info.skinned ? "SkinnedMesh" : "StaticMesh");
    if (!info.rigType.empty())
        typeNode.addChild("Rig").values.emplace_back(info.rigType);

    const Transform& lt = info.localTransform;
    if (lt.pos != glm::vec3(0.0f))         out.set("Position", lt.pos);
    if (lt.quat != glm::quat(1, 0, 0, 0))  out.set("Rotation", glm::degrees(glm::eulerAngles(lt.quat)));
    if (lt.scale != 1.0f)                  out.set("Scale", lt.scale);
}

const AnimationSet* World::getOrBuildClipSet(const Skeleton* skel, const AnimatorDesc& desc)
{
    const std::string key = std::to_string(reinterpret_cast<uintptr_t>(skel)) + "/" + desc.name;
    if (auto it = m_clipSets.find(key); it != m_clipSets.end())
        return it->second.get();

    auto set = std::make_unique<AnimationSet>();
    AnimationSet& clips = *set;

    // Apply the .anm's loop flag + event notifies onto the clip just loaded under `localName`.
    auto applyClipMeta = [&](const AnimationClipDesc& anm, const std::string& localName)
    {
        const auto idxIt = clips.nameToIndex.find(localName);
        if (idxIt == clips.nameToIndex.end())
            return;
        AnimationClip& clip = clips.clips[idxIt->second];
        clip.loop = anm.loop;
        clip.events.clear();
        for (const auto& [name, t] : anm.events)
            clip.events.push_back({ name, t });
    };

    // Loads one .anm into the set under `localName` (retargeted by bone name to `skel`).
    auto loadClipDesc = [&](const AnimationClipDesc& anm, const std::string& localName)
    {
        std::string sourcePath = anm.source;
        if (const ObjectContainerDesc* oc = Globals::assetRegistry.findObjectContainer(anm.source))
            sourcePath = oc->path; // source named a registered container; use its file
        const char* skip = anm.skip.empty() ? nullptr : anm.skip.c_str();
        const char* track = anm.track.empty() ? nullptr : anm.track.c_str();
        ISceneData::loadAnimations(sourcePath.c_str(), *skel, clips, skip, localName.c_str(), track);
        applyClipMeta(anm, localName);
    };

    // Explicit `Clip <local> Anim <anm>` declarations are optional: they alias a clip to a local name (or
    // force-load one nothing references). Anything not declared is lazy-loaded by name below.
    for (const AnimatorDesc::ClipRef& ref : desc.clips)
    {
        if (const AnimationClipDesc* anm = Globals::assetRegistry.findClip(ref.anmName))
            loadClipDesc(*anm, ref.localName);
        else
            Log::warning("Animator '" + desc.name + "': unknown Animation clip '" + ref.anmName + "'");
    }

    // Resolve a clip by name, lazily loading it from the .anm registry when it wasn't declared with `Clip`.
    auto ensureClip = [&](const std::string& name)
    {
        if (name.empty() || clips.find(name))
            return;
        if (const AnimationClipDesc* anm = Globals::assetRegistry.findClip(name))
            loadClipDesc(*anm, name);
        else
            Log::warning("Animator '" + desc.name + "': unknown clip '" + name + "'");
    };

    std::unordered_set<std::string> blendNames;
    for (const AnimatorDesc::BlendSpace& bs : desc.blendSpaces)
        blendNames.insert(bs.name);
    for (const AnimatorDesc::BlendSpace& bs : desc.blendSpaces)
        for (const AnimatorDesc::BlendSample& s : bs.samples)
            ensureClip(s.clip);
    for (const AnimatorDesc::State& st : desc.stateMachine.states)
        if (!blendNames.contains(st.play))
            ensureClip(st.play);

    Log::info("Animator '" + desc.name + "': built clip set (" + std::to_string(clips.numClips()) + " clips)");

    const AnimationSet* ptr = set.get();
    m_clipSets.emplace(key, std::move(set));
    return ptr;
}

std::shared_ptr<AnimatorComponent::SpawnInfo> World::buildAnimatorSpawnInfo(const AssetNode& animatorNode, const std::string& siblingContainerName, const std::string& ownerName)
{
    const AssetNode* nameNode = animatorNode.find("Animator");
    if (!nameNode)
        return nullptr;
    const std::string animatorName = nameNode->asString();

    const AnimatorDesc* desc = Globals::assetRegistry.findAnimator(animatorName);
    if (!desc)
    {
        Log::warning("Scene: entity '" + ownerName + "' references unknown Animator '" + animatorName + "'");
        return nullptr;
    }
    ObjectContainer* siblingContainer = siblingContainerName.empty() ? nullptr : getOrLoadContainer(siblingContainerName);
    if (!siblingContainer || !siblingContainer->isSkinned() || !siblingContainer->getSkeleton())
    {
        Log::warning("Scene: entity '" + ownerName + "' has an Animator but no sibling skinned mesh to drive");
        return nullptr;
    }

    auto info = std::make_shared<AnimatorComponent::SpawnInfo>();
    info->desc = desc;
    info->skeleton = siblingContainer->getSkeleton();
    info->clipSet = getOrBuildClipSet(info->skeleton, *desc); // shared, imported once per skeleton+animator
    info->animatorName = animatorName;
    if (const AssetNode* n = animatorNode.find("Enabled"))
        info->enabled = n->asBool();
    return info;
}

std::shared_ptr<SceneComponent::SpawnInfo> World::buildSceneSpawnInfo(const AssetNode& sceneNode)
{
    auto info = std::make_shared<SceneComponent::SpawnInfo>();
    if (const AssetNode* n = sceneNode.find("Enabled")) info->enabled = n->asBool();

    for (const AssetNode& child : sceneNode.children)
    {
        std::shared_ptr<const EntitySpawnTemplate> childTmpl;
        if (keyIs(child, "Entity"))
            childTmpl = buildInlineTemplate(child);
        else if (keyIs(child, "Prefab"))
            childTmpl = getOrBuildPrefabTemplate(child.asString());
        else
            continue;
        if (!childTmpl)
            continue;

        SceneComponent::SpawnInfo::ChildSpawnInfo ci;
        ci.tmpl = std::move(childTmpl);
        ci.localTransform = readNodeTransform(child);
        if (const AssetNode* n = child.find("Name")) ci.name = n->asString();
        info->children.push_back(std::move(ci));
    }
    return info;
}

// Appends a snapshot node subtree's meshes into collision space. Mirrors ObjectContainer's spawn
// rebasing: a ROOT spawn keeps the scene root's own transform (baked into the render offsets), while a
// sub-node spawn excludes the start node's transform (the entity transform places the node's pivot).
static void appendNodeGeometry(const CollisionSource& source, const CollisionSource::Node& node,
    const glm::mat4& parentTransform, bool skipOwnTransform, PhysicsGeometry& outGeometry)
{
    const glm::mat4 transform = skipOwnTransform ? parentTransform : parentTransform * node.localTransform;
    const bool nodeIsProxy = isCollisionName(node.name);
    for (uint32 meshIdx : node.meshIndices)
    {
        const CollisionSource::Mesh& mesh = source.meshes[meshIdx];
        // "Col_*" proxy meshes always collide; a render mesh is skipped when a proxy exists for its
        // (or its node's) name — the proxy replaces it.
        if (!nodeIsProxy && !isCollisionName(mesh.name)
            && (source.proxiedNames.contains(mesh.name) || source.proxiedNames.contains(node.name)))
            continue;
        const uint32 baseVertex = uint32(outGeometry.vertices.size());
        for (const glm::vec3& v : mesh.vertices)
            outGeometry.vertices.push_back(glm::vec3(transform * glm::vec4(v, 1.0f)));
        for (uint32 index : mesh.indices)
            outGeometry.indices.push_back(baseVertex + index);
    }
    for (const CollisionSource::Node& child : node.children)
        appendNodeGeometry(source, child, transform, false, outGeometry);
}

static const CollisionSource::Node* findNodeByName(const CollisionSource::Node& node, std::string_view name)
{
    for (const CollisionSource::Node& child : node.children)
    {
        if (name == child.name)
            return &child;
        if (const CollisionSource::Node* found = findNodeByName(child, name))
            return found;
    }
    return nullptr;
}

static PhysicsGeometry buildCollisionGeometry(const CollisionSource& source, const std::string& containerName, const std::string& nodePath)
{
    const CollisionSource::Node* startNode = &source.root;
    if (!nodePath.empty() && nodePath != "ROOT")
    {
        if (const CollisionSource::Node* found = findNodeByName(source.root, nodePath.substr(nodePath.find_last_of('/') + 1)))
            startNode = found;
        else
            Log::warning("Physics: node '" + nodePath + "' not found in '" + containerName + "', using ROOT");
    }
    PhysicsGeometry geometry;
    appendNodeGeometry(source, *startNode, glm::mat4(1.0f), startNode != &source.root, geometry);
    return geometry;
}

std::shared_ptr<const CollisionSource> World::getOrLoadCollisionSource(const std::string& containerName)
{
    if (auto it = m_collisionSources.find(containerName); it != m_collisionSources.end())
        return it->second;

    // Fallback: the container was loaded before physics needed geometry (or isn't loaded at all),
    // so the source file is imported once more and the snapshot cached for any further requests.
    const ObjectContainerDesc* desc = Globals::assetRegistry.findObjectContainer(containerName);
    if (!desc)
    {
        Log::warning("Physics: unknown ObjectContainer '" + containerName + "' for collision geometry");
        return nullptr;
    }
    std::unique_ptr<ISceneData> sceneData = desc->procedural
        ? ISceneData::createProceduralLoader()
        : ISceneData::createAssimpLoader();
    if (!sceneData->initialize(desc->path.c_str(), desc->mergeNodes, desc->preTransformVertices))
    {
        Log::warning("Physics: failed to load '" + desc->path + "' for collision geometry");
        return nullptr;
    }
    Log::info("Physics: container '" + containerName + "' re-imported for collision geometry (loaded before physics needed it)");

    std::shared_ptr<const CollisionSource> source = buildCollisionSource(*sceneData);
    m_collisionSources.emplace(containerName, source);
    return source;
}

std::shared_ptr<PhysicsMesh> World::getOrBuildCollisionMesh(const std::string& containerName, const std::string& nodePath)
{
    const std::string key = containerName + "|" + nodePath;
    if (auto it = m_collisionMeshes.find(key); it != m_collisionMeshes.end())
        return it->second;
    std::shared_ptr<const CollisionSource> source = getOrLoadCollisionSource(containerName);
    if (!source)
        return nullptr;
    const PhysicsGeometry geometry = buildCollisionGeometry(*source, containerName, nodePath);
    if (geometry.indices.size() < 3)
        return nullptr;
    glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
    for (const glm::vec3& v : geometry.vertices)
    {
        boundsMin = glm::min(boundsMin, v);
        boundsMax = glm::max(boundsMax, v);
    }
    Log::info(std::format("Physics: collision mesh '{}': {} verts, {} tris, bounds ({:.2f}, {:.2f}, {:.2f}) - ({:.2f}, {:.2f}, {:.2f})",
        key, geometry.vertices.size(), geometry.indices.size() / 3,
        boundsMin.x, boundsMin.y, boundsMin.z, boundsMax.x, boundsMax.y, boundsMax.z));
    auto mesh = std::make_shared<PhysicsMesh>(Globals::physics.createCollisionMesh(geometry.vertices, geometry.indices));
    if (!mesh->isValid())
        return nullptr;
    m_collisionMeshes.emplace(key, mesh);
    return mesh;
}

std::shared_ptr<PhysicsComponent::SpawnInfo> World::buildPhysicsSpawnInfo(const AssetNode& physicsNode,
    const std::string& containerName, const std::string& nodePath, const std::string& ownerName)
{
    auto info = std::make_shared<PhysicsComponent::SpawnInfo>();
    if (const AssetNode* n = physicsNode.find("Body"))
    {
        const std::string& type = n->asString();
        if (type == "Static")         info->bodyType = EPhysicsBodyType::Static;
        else if (type == "Kinematic") info->bodyType = EPhysicsBodyType::Kinematic;
        else                          info->bodyType = EPhysicsBodyType::Dynamic;
    }
    if (const AssetNode* n = physicsNode.find("Shape"))
    {
        const std::string& type = n->asString();
        if (type == "Sphere")       info->shape.type = EPhysicsShapeType::Sphere;
        else if (type == "Capsule") info->shape.type = EPhysicsShapeType::Capsule;
        else if (type == "Hull")    info->shape.type = EPhysicsShapeType::Hull;
        else if (type == "Mesh")    info->shape.type = EPhysicsShapeType::Mesh;
        else                        info->shape.type = EPhysicsShapeType::Box;
    }
    PhysicsShape& shape = info->shape;

    // Hull/Mesh pull their geometry from the sibling render mesh's container.
    if (shape.type == EPhysicsShapeType::Hull)
    {
        if (!containerName.empty())
            if (std::shared_ptr<const CollisionSource> source = getOrLoadCollisionSource(containerName))
                shape.hullPoints = buildCollisionGeometry(*source, containerName, nodePath).vertices;
        if (shape.hullPoints.size() < 4)
        {
            Log::warning("Scene: entity '" + ownerName + "' has a Hull physics shape but no render geometry, using Box");
            shape.type = EPhysicsShapeType::Box;
        }
    }
    else if (shape.type == EPhysicsShapeType::Mesh)
    {
        if (!containerName.empty())
            info->mesh = getOrBuildCollisionMesh(containerName, nodePath);
        if (info->mesh)
        {
            shape.mesh = info->mesh.get();
        }
        else
        {
            Log::warning("Scene: entity '" + ownerName + "' has a Mesh physics shape but no render geometry, using Box");
            shape.type = EPhysicsShapeType::Box;
        }
    }
    if (const AssetNode* n = physicsNode.find("HalfExtents")) shape.halfExtents = n->asVec3(shape.halfExtents);
    if (const AssetNode* n = physicsNode.find("Radius"))      shape.radius = n->asFloat(0, shape.radius);
    if (const AssetNode* n = physicsNode.find("HalfHeight"))  shape.halfHeight = n->asFloat(0, shape.halfHeight);
    if (const AssetNode* n = physicsNode.find("Offset"))      shape.offset = n->asVec3(shape.offset);
    if (const AssetNode* n = physicsNode.find("Density"))     shape.density = n->asFloat(0, shape.density);
    if (const AssetNode* n = physicsNode.find("Friction"))    shape.friction = n->asFloat(0, shape.friction);
    if (const AssetNode* n = physicsNode.find("Restitution")) shape.restitution = n->asFloat(0, shape.restitution);
    if (const AssetNode* n = physicsNode.find("Layer"))
    {
        info->layer = n->asString();
        shape.categoryBits = PhysicsLayers::bit(info->layer);
    }
    if (const AssetNode* n = physicsNode.find("CollidesWith"))
    {
        uint64 mask = 0;
        for (size_t i = 0; i < n->numValues(); ++i)
        {
            const std::string& name = n->asString(i);
            if (name == "All")        mask = PhysicsLayers::All;
            else if (name != "None")  mask |= PhysicsLayers::bit(name);
            info->collidesWith.push_back(name);
        }
        shape.maskBits = mask;
    }
    if (const AssetNode* n = physicsNode.find("Group"))       shape.groupIndex = n->asInt();
    if (const AssetNode* n = physicsNode.find("MaxHullVertices")) shape.maxHullVertices = n->asInt(0, shape.maxHullVertices);
    if (const AssetNode* n = physicsNode.find("Sensor"))        shape.isSensor = n->asBool();
    if (const AssetNode* n = physicsNode.find("ContactEvents")) shape.contactEvents = n->asBool();
    if (const AssetNode* n = physicsNode.find("Enabled"))     info->enabled = n->asBool();
    return info;
}

std::shared_ptr<AudioBuffer> World::getOrLoadAudioBuffer(const std::string& path)
{
    if (auto it = m_audioBuffers.find(path); it != m_audioBuffers.end())
        return it->second;
    auto buffer = std::make_shared<AudioBuffer>(Globals::audio.loadSound(path));
    m_audioBuffers.emplace(path, buffer);
    return buffer;
}

std::shared_ptr<AudioComponent::SpawnInfo> World::buildAudioSpawnInfo(const AssetNode& audioNode, const std::string& ownerName)
{
    auto info = std::make_shared<AudioComponent::SpawnInfo>();
    for (const AssetNode& soundNode : audioNode.children)
    {
        if (!keyIs(soundNode, "Sound"))
            continue;
        AudioComponent::SoundDesc sound;
        sound.alias = soundNode.asString();
        if (const AssetNode* n = soundNode.find("Select"))
            sound.select = audioSelectFromToken(n->asString());

        // Each `Path` child is one clip; its own child lines (Volume/Pitch/...) are the clip's settings.
        for (const AssetNode& pathNode : soundNode.children)
        {
            if (!keyIs(pathNode, "Path"))
                continue;
            AudioComponent::Clip clip;
            clip.path = pathNode.asString();
            if (clip.path.empty())
                continue;
            if (const AssetNode* n = pathNode.find("Volume"))            clip.volume = n->asFloat(0, clip.volume);
            if (const AssetNode* n = pathNode.find("Pitch"))             clip.pitch = n->asFloat(0, clip.pitch);
            if (const AssetNode* n = pathNode.find("Loop"))              clip.loop = n->asBool();
            if (const AssetNode* n = pathNode.find("Relative"))          clip.relative = n->asBool();
            if (const AssetNode* n = pathNode.find("ReferenceDistance")) clip.referenceDistance = n->asFloat(0, clip.referenceDistance);
            if (const AssetNode* n = pathNode.find("MaxDistance"))       clip.maxDistance = n->asFloat(0, clip.maxDistance);
            if (const AssetNode* n = pathNode.find("Rolloff"))           clip.rolloff = n->asFloat(0, clip.rolloff);
            clip.buffer = getOrLoadAudioBuffer(clip.path); // may be invalid (load failure logged); alias stays triggerable as a no-op
            sound.clips.push_back(std::move(clip));
        }
        if (sound.alias.empty() || sound.clips.empty())
        {
            Log::warning("Scene: entity '" + ownerName + "' has an audio Sound entry without an alias or Path, skipping");
            continue;
        }
        info->sounds.push_back(std::move(sound));
    }
    if (info->sounds.empty())
        return nullptr;
    return info;
}

void World::buildTemplate(const AssetNode& node, EntitySpawnTemplate& tmpl)
{
    tmpl.defaultTransform = readNodeTransform(node); // the declaration's authored placement
    const AssetNode* nameNode = node.find("Name");
    tmpl.displayName = nameNode ? nameNode->asString() : node.asString();

    uint16 typeBits = 0;

    static_assert(EComponentID_Scene == 0);
    if (const AssetNode* sceneNode = findComponentNode(node, "Scene"))
        if (std::shared_ptr<SceneComponent::SpawnInfo> info = buildSceneSpawnInfo(*sceneNode))
        {
            typeBits |= uint16(1 << EComponentID_Scene);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    static_assert(EComponentID_Render == 3);
    // A Hull/Mesh physics shape (parsed below) sources its geometry from the render container, so the
    // collision snapshot is captured while the container's scene data is loaded — one import, not two.
    const AssetNode* physicsNode = findComponentNode(node, "Physics");
    bool wantsCollisionGeometry = false;
    if (physicsNode)
        if (const AssetNode* n = physicsNode->find("Shape"))
            wantsCollisionGeometry = n->asString() == "Hull" || n->asString() == "Mesh";

    std::string renderContainerName; // physics hull/mesh shapes (below), and the animator, source from here
    std::string renderNodePath;
    if (const AssetNode* renderNode = findComponentNode(node, "Render"))
        if (std::shared_ptr<RenderComponent::SpawnInfo> info = buildRenderSpawnInfo(*renderNode, tmpl.displayName, wantsCollisionGeometry))
        {
            renderContainerName = info->containerName;
            renderNodePath = info->nodePath;
            typeBits |= uint16(1 << EComponentID_Render);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    static_assert(EComponentID_Animator == 4);
    if (const AssetNode* animatorNode = findComponentNode(node, "Animator"))
        if (std::shared_ptr<AnimatorComponent::SpawnInfo> info = buildAnimatorSpawnInfo(*animatorNode, renderContainerName, tmpl.displayName))
        {
            typeBits |= uint16(1 << EComponentID_Animator);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    static_assert(EComponentID_Physics == 5);
    if (physicsNode) // found above, before the render container load
    {
        typeBits |= uint16(1 << EComponentID_Physics);
        tmpl.spawnInfos.emplace_back(buildPhysicsSpawnInfo(*physicsNode, renderContainerName, renderNodePath, tmpl.displayName));
    }

    static_assert(EComponentID_Audio == 6);
    if (const AssetNode* audioNode = findComponentNode(node, "Audio"))
        if (std::shared_ptr<AudioComponent::SpawnInfo> info = buildAudioSpawnInfo(*audioNode, tmpl.displayName))
        {
            typeBits |= uint16(1 << EComponentID_Audio);
            tmpl.spawnInfos.emplace_back(std::move(info));
        }

    static_assert(EComponentID_Script == 7);
    if (const AssetNode* scriptNode = findComponentNode(node, "Script"))
    {
        auto info = std::make_shared<ScriptComponent::SpawnInfo>();
        if (const AssetNode* n = scriptNode->find("Path"))    info->scriptPath = n->asString();
        if (const AssetNode* n = scriptNode->find("Enabled")) info->enabled = n->asBool();
        typeBits |= uint16(1 << EComponentID_Script);
        tmpl.spawnInfos.emplace_back(std::move(info));
    }

    tmpl.archetype = makeEntityArchetype(typeBits);
}

std::shared_ptr<const EntitySpawnTemplate> World::cacheTemplate(const std::string& name, const std::string& sourceFile, const AssetNode& node)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
        return it->second;

    m_buildingTemplates.insert(name);
    auto tmpl = std::make_shared<EntitySpawnTemplate>();
    tmpl->sourceFile = sourceFile;
    tmpl->prefabName = name; // a registered prefab name, so a re-serialized instance writes "Prefab <name>"
    buildTemplate(node, *tmpl);
    m_buildingTemplates.erase(name);

    m_templates.emplace(name, tmpl); // heap-owned: address stays stable for child back-pointers
    return tmpl;
}

std::shared_ptr<const EntitySpawnTemplate> World::buildInlineTemplate(const AssetNode& node)
{
    auto tmpl = std::make_shared<EntitySpawnTemplate>();
    buildTemplate(node, *tmpl);
    return tmpl;
}

std::shared_ptr<const EntitySpawnTemplate> World::buildFileTemplate(const std::string& path)
{
    AssetNode doc;
    std::string error;
    if (!loadAssetFile(path, doc, error))
    {
        Log::warning("Scene: asset load failed: " + error);
        return nullptr;
    }

    for (const AssetNode& decl : doc.children)
        if (keyIs(decl, "Prefab"))
            return cacheTemplate(decl.asString(), path, decl);

    Log::warning("Scene: asset '" + path + "' declared no prefab");
    return nullptr;
}

std::shared_ptr<const EntitySpawnTemplate> World::getOrBuildPrefabTemplate(const std::string& name)
{
    if (auto it = m_templates.find(name); it != m_templates.end())
        return it->second; // cache hit: no asset file touched

    if (m_buildingTemplates.contains(name))
    {
        Log::warning("Scene: prefab cycle detected at '" + name + "', skipping");
        return nullptr;
    }

    if (const std::string* prefabPath = Globals::assetRegistry.findPrefab(name))
    {
        if (std::shared_ptr<const EntitySpawnTemplate> tmpl = buildFileTemplate(*prefabPath))
            return tmpl; // its declared root name == `name`
        Log::warning("Scene: prefab '" + name + "' not declared in '" + *prefabPath + "', skipping");
        return nullptr;
    }

    Log::warning("Scene: references unknown prefab '" + name + "', skipping");
    return nullptr;
}

EntityPtr World::spawnAssetFile(const std::string& path, const Transform& base, bool overrideDefaultTransform)
{
    std::error_code ec;
    const std::filesystem::path relativePath = std::filesystem::relative(path, ec);
    const std::string fileName = (ec || relativePath.empty()) ? path : relativePath.string();

    const std::string* rootName = Globals::assetRegistry.findRootForFile(fileName);
    std::shared_ptr<const EntitySpawnTemplate> tmpl = rootName ? getOrBuildPrefabTemplate(*rootName) : buildFileTemplate(fileName);
    if (!tmpl)
        return EntityPtr{};

    const Transform& dt = tmpl->defaultTransform;
    const glm::vec3 pos = overrideDefaultTransform ? base.pos : dt.pos;
    return Entity::create(*tmpl, Transform(pos, dt.scale, dt.quat));
}

EntityPtr World::createEmptyEntity(const std::string& name)
{
    // A blank Scene-only template with no prefabName: Entity::create leaves prefabInstance false, so the
    // entity is editable and serializes inline. Cached (and kept across reloadPrefabs) so its address
    // stays stable for the entities that point at it.
    if (!m_emptyTemplate)
    {
        m_emptyTemplate = std::make_shared<EntitySpawnTemplate>();
        m_emptyTemplate->archetype = makeEntityArchetype(0);
        m_emptyTemplate->displayName = "Entity";
    }
    EntityPtr entity = Entity::create(*m_emptyTemplate, Transform());
    entity->displayName = name;
    return entity;
}
