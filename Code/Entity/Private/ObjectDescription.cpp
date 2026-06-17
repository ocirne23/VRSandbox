module Entity.ObjectDescription;


static char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; }

static bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (lower(a[i]) != lower(b[i]))
            return false;
    return true;
}

const std::string& ComponentDesc::property(std::string_view key) const
{
    static const std::string empty;
    const AssetNode* p = node.find(key);
    return p ? p->asString() : empty;
}

float ComponentDesc::floatProperty(std::string_view key, float fallback) const
{
    const AssetNode* p = node.find(key);
    return p ? p->asFloat(0, fallback) : fallback;
}

glm::vec3 ComponentDesc::vec3Property(std::string_view key, const glm::vec3& fallback) const
{
    const AssetNode* p = node.find(key);
    return p ? p->asVec3(fallback) : fallback;
}

const ComponentDesc* EntityDesc::findComponent(std::string_view type) const
{
    for (const ComponentDesc& component : components)
        if (iequals(component.type, type))
            return &component;
    return nullptr;
}

bool toObjectContainerDesc(const AssetNode& node, ObjectContainerDesc& out)
{
    if (!iequals(node.key, "ObjectContainer"))
        return false;

    out = ObjectContainerDesc{};
    out.name = node.asString(0);
    if (const AssetNode* p = node.find("Path"))
        out.path = p->asString();
    if (const AssetNode* p = node.find("Loader"))
        out.procedural = iequals(p->asString(), "Procedural");
    if (const AssetNode* p = node.find("MergeNodes"))
        out.mergeNodes = p->asBool();
    if (const AssetNode* p = node.find("PreTransformVertices"))
        out.preTransformVertices = p->asBool();

    if (const AssetNode* mo = node.find("MaterialOverrides"))
    {
        MaterialOverridesDesc& ov = out.materialOverrides;
        ov.present = true;
        if (const AssetNode* p = mo->find("PipelineIdx"))
            ov.pipeline = p->asString();
        if (const AssetNode* p = mo->find("ExcludeFromRayTracing"))
            ov.excludeFromRayTracing = p->asBool();
        if (const AssetNode* p = mo->find("UseSceneTextures"))
            ov.useSceneTextures = p->asBool();
        if (const AssetNode* p = mo->find("DiffuseTexIdx"))
            ov.diffuseTexIdx = p->asInt();
        if (const AssetNode* p = mo->find("NormalTexIdx"))
            ov.normalTexIdx = p->asInt();
        if (const AssetNode* p = mo->find("MetalRoughnessTexIdx"))
            ov.metalRoughnessTexIdx = p->asInt();
    }
    return true;
}

bool toEntityDesc(const AssetNode& node, EntityDesc& out)
{
    if (!iequals(node.key, "Entity"))
        return false;

    out = EntityDesc{};
    out.name = node.asString(0);
    for (const AssetNode* component : node.findAll("Component"))
    {
        ComponentDesc desc;
        desc.type = component->asString(0);
        desc.node = *component;
        out.components.push_back(std::move(desc));
    }
    return true;
}

bool loadObjectContainerDesc(const std::string& path, ObjectContainerDesc& out, std::string& outError)
{
    AssetNode root;
    if (!loadAssetFile(path, root, outError))
        return false;
    const AssetNode* top = root.find("ObjectContainer");
    if (!top)
    {
        outError = "No ObjectContainer declaration in: " + path;
        return false;
    }
    return toObjectContainerDesc(*top, out);
}

bool loadEntityDesc(const std::string& path, EntityDesc& out, std::string& outError)
{
    AssetNode root;
    if (!loadAssetFile(path, root, outError))
        return false;
    const AssetNode* top = root.find("Entity");
    if (!top)
    {
        outError = "No Entity declaration in: " + path;
        return false;
    }
    return toEntityDesc(*top, out);
}