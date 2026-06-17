export module Entity.ObjectDescription;

import Core;
import Core.glm;
import File.AssetParser;


// Optional material overrides parsed from an ".oc" file's "MaterialOverrides" block. Kept
// RendererVK-free here (pipeline named as a string, tex indices as -1 = "leave default"); the
// World translates this into a RendererVK ObjectContainer::MaterialOverrides when loading.
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

// Parsed ".oc" file: describes how to load an ObjectContainer from a model file (Assimp) or a
// built-in procedural shape ("skysphere", "terrain", "cube", ...).
export struct ObjectContainerDesc
{
    std::string name;
    std::string path;                    // model file path, or procedural shape name
    bool procedural = false;             // Loader: Procedural vs Assimp (default)
    bool mergeNodes = false;
    bool preTransformVertices = false;
    MaterialOverridesDesc materialOverrides;
};

// Build a typed description from an already-parsed top-level "ObjectContainer" declaration node.
// Returns false if the node is not an ObjectContainer.
export bool toObjectContainerDesc(const AssetNode& node, ObjectContainerDesc& out);

// Load + parse + interpret a file in one call. Returns false (with outError) on read failure
// or when the file contains no ObjectContainer declaration.
export bool loadObjectContainerDesc(const std::string& path, ObjectContainerDesc& out, std::string& outError);
