module Entity;

import Core;
import Core.glm;
import File;

import :ObjectDescription;

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

bool toSpawnableDesc(const AssetNode& node, const std::string& containerName, SpawnableDesc& out)
{
    const bool skinned = iequals(node.key, "SkinnedMesh");
    if (!skinned && !iequals(node.key, "StaticMesh"))
        return false;

    out = SpawnableDesc{};
    out.name = node.asString(0);
    out.containerName = containerName;
    out.skinned = skinned;
    if (const AssetNode* n = node.find("Node"))
        out.node = n->asString();
    if (skinned)
    {
        if (const AssetNode* t = node.find("Type"))
            out.rigType = t->asString();
        if (const AssetNode* bm = node.find("BoneMapping"))
            for (const AssetNode& pair : bm->children)
                out.boneMapping.emplace_back(pair.key, pair.asString(0)); // canonical -> rig bone
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
