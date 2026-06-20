export module File:IMeshData;

import File.fwd;

import Core;
import Core.glm;
import Core.AABB;

export class IMeshData
{
public:
    virtual ~IMeshData() = default;


    virtual const glm::vec3* getVertices() const = 0;
    virtual const glm::vec3* getNormals() const = 0;
    virtual const glm::vec3* getTangents() const = 0;
    virtual const glm::vec3* getBitangents() const = 0;
    virtual const glm::vec3* getTexCoords() const = 0;
    virtual const uint32* getIndices() const = 0;
    virtual uint32 getNumVertices() const = 0;
    virtual uint32 getNumIndices() const = 0;
    virtual uint32 getMaterialIndex() const = 0;
    virtual AABB getAABB() const = 0;
    virtual const char* getName() const = 0;

    // Skinning: per-vertex bone influences (up to 4). Indices reference the scene Skeleton's bone array
    // (see ISceneData::getSkeleton). Default: not skinned. Arrays, when present, are getNumVertices() long.
    virtual bool isSkinned() const { return false; }
    virtual const glm::uvec4* getBoneIndices() const { return nullptr; }
    virtual const glm::vec4* getBoneWeights() const { return nullptr; }
};