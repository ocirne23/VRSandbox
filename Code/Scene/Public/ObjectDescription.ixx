export module Scene.ObjectDescription;

import Core;
import Core.glm;
import Scene.AssetParser;

export namespace Scene
{
    // Optional material overrides parsed from an ".oc" file's "MaterialOverrides" block. Kept
    // RendererVK-free here (pipeline named as a string, tex indices as -1 = "leave default"); the
    // World translates this into a RendererVK ObjectContainer::MaterialOverrides when loading.
    struct MaterialOverridesDesc
    {
        bool present = false;
        std::string pipeline;                // "LitOpaque" / "UnlitOpaque" / "Sky" / ... (empty = default)
        bool excludeFromRayTracing = false;
        bool useSceneTextures = false;
        int diffuseTexIdx = -1;              // -1 leaves the override default in place
        int normalTexIdx = -1;
        int metalRoughnessTexIdx = -1;
    };

    // Parsed ".oc" file: describes how to load an ObjectContainer from a model file (Assimp) or a
    // built-in procedural shape ("skysphere", "terrain", "cube", ...).
    struct ObjectContainerDesc
    {
        std::string name;
        std::string path;                    // model file path, or procedural shape name
        bool procedural = false;             // Loader: Procedural vs Assimp (default)
        bool mergeNodes = false;
        bool preTransformVertices = false;
        MaterialOverridesDesc materialOverrides;
    };

    // One component of an entity. The type names the component; the parsed subtree is kept
    // so callers can read arbitrary properties (Position, Rotation, Name, ...) by key.
    struct ComponentDesc
    {
        std::string type;
        AssetNode node;

        const AssetNode* find(std::string_view key) const { return node.find(key); }
        const std::string& property(std::string_view key) const;
        float floatProperty(std::string_view key, float fallback = 0.0f) const;
        glm::vec3 vec3Property(std::string_view key, const glm::vec3& fallback = glm::vec3(0.0f)) const;
    };

    // Parsed ".ent" file: a named entity with an ordered list of components.
    struct EntityDesc
    {
        std::string name;
        std::vector<ComponentDesc> components;

        const ComponentDesc* findComponent(std::string_view type) const;
    };

    // Build a typed description from an already-parsed top-level declaration node.
    // Returns false if the node is not the expected kind ("ObjectContainer" / "Entity").
    bool toObjectContainerDesc(const AssetNode& node, ObjectContainerDesc& out);
    bool toEntityDesc(const AssetNode& node, EntityDesc& out);

    // Load + parse + interpret a file in one call. Returns false (with outError) on read failure
    // or when the file contains no declaration of the expected kind.
    bool loadObjectContainerDesc(const std::string& path, ObjectContainerDesc& out, std::string& outError);
    bool loadEntityDesc(const std::string& path, EntityDesc& out, std::string& outError);
}
