export module Entity:ObjectDescription;

import Core;
import Core.glm;
import File;

export struct MaterialOverridesDesc
{
    bool present = false;
    std::string pipeline;                // "LitOpaque" / "UnlitOpaque" / "Sky" / ... (empty = default)
    bool excludeFromRayTracing = false;
    bool useSceneTextures = true;
    int diffuseTexIdx = -1;              // -1 leaves the override default in place
    int normalTexIdx = -1;
    int metalRoughnessTexIdx = -1;
};

export struct ObjectContainerDesc
{
    std::string name;
    std::string path;                    // model file path, or procedural shape name
    bool procedural = false;             // Loader: Procedural vs Assimp (default)
    bool mergeNodes = false;
    bool preTransformVertices = false;
    MaterialOverridesDesc materialOverrides;
};

// A named mesh that can be spawned from an ObjectContainer (declared as a `StaticMesh`/`SkinnedMesh` entry
// alongside the `ObjectContainer` block in a .oc file). Referenced by name from a prefab's RenderNode.
export struct SpawnableDesc
{
    std::string name;
    std::string containerName;           // owning ObjectContainer (the one declared in the same .oc file)
    std::string node = "ROOT";           // node path within the model
    bool skinned = false;                // StaticMesh -> false, SkinnedMesh -> true
    std::string rigType;                 // skinned only: "Humanoid" / "Generic" (empty = unspecified)
    std::vector<std::pair<std::string, std::string>> boneMapping; // skinned: canonical bone -> rig bone name
};

export bool toObjectContainerDesc(const AssetNode& node, ObjectContainerDesc& out);

// node.key must be "StaticMesh" or "SkinnedMesh"; containerName is the ObjectContainer it belongs to.
export bool toSpawnableDesc(const AssetNode& node, const std::string& containerName, SpawnableDesc& out);

export bool loadObjectContainerDesc(const std::string& path, ObjectContainerDesc& out, std::string& outError);
