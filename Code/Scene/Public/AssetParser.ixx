export module Scene.AssetParser;

import Core;
import Core.glm;

export namespace Scene
{
    // One line of a parsed asset file: a key, the values that followed it on the same line,
    // and any deeper-indented lines as child nodes. The whole file is represented as a synthetic
    // root AssetNode whose children are the top-level declarations.
    //
    // Grammar (indentation defines hierarchy, tabs or spaces):
    //   Key value value ...        -> key + values
    //   "quoted value"             -> a single value, may contain spaces
    //   0, 0, 0                    -> commas are treated as separators (vectors)
    //   # ... or // ...            -> comment line
    struct AssetNode
    {
        std::string key;
        std::vector<std::string> values;
        std::vector<AssetNode> children;

        bool hasValue(size_t idx = 0) const { return idx < values.size(); }
        size_t numValues() const { return values.size(); }

        const std::string& asString(size_t idx = 0) const
        {
            static const std::string empty;
            return idx < values.size() ? values[idx] : empty;
        }
        bool asBool(size_t idx = 0, bool fallback = false) const;
        int asInt(size_t idx = 0, int fallback = 0) const;
        float asFloat(size_t idx = 0, float fallback = 0.0f) const;
        glm::vec3 asVec3(const glm::vec3& fallback = glm::vec3(0.0f)) const;

        // Case-insensitive lookup of a direct child by key (keys are human-authored).
        const AssetNode* find(std::string_view childKey) const;
        std::vector<const AssetNode*> findAll(std::string_view childKey) const;
    };

    // Parses indentation-structured text into outRoot. Lenient: always succeeds for well-formed
    // text; outError is reserved for future structural diagnostics.
    bool parseAssetText(std::string_view text, AssetNode& outRoot, std::string& outError);

    // Reads a file (path relative to the Assets working dir) and parses it.
    // Returns false and fills outError if the file cannot be read.
    bool loadAssetFile(const std::string& path, AssetNode& outRoot, std::string& outError);
}
