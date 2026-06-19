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

export bool toObjectContainerDesc(const AssetNode& node, ObjectContainerDesc& out);

export bool loadObjectContainerDesc(const std::string& path, ObjectContainerDesc& out, std::string& outError);
