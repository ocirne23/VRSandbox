export module File.IMaterialData;

import Core;
import Core.glm;
import File.fwd;

export class IMaterialData
{
public:
    virtual ~IMaterialData() = default;
	enum class ETextureType // see Assimp docs, matches aiTextureType
    {
        NONE = 0,
        DIFFUSE = 1,
        SPECULAR = 2,
        AMBIENT = 3,
        EMISSIVE = 4,
        HEIGHT = 5,
        NORMALS = 6,
        SHININESS = 7,
        OPACITY = 8,
        DISPLACEMENT = 9,
        LIGHTMAP = 10,
        REFLECTION = 11,
        BASE_COLOR = 12,
        NORMAL_CAMERA = 13,
        EMISSION_COLOR = 14,
        METALNESS = 15,
        DIFFUSE_ROUGHNESS = 16,
        AMBIENT_OCCLUSION = 17,
        aiTextureType_SHEEN = 19,
        aiTextureType_CLEARCOAT = 20,
        aiTextureType_TRANSMISSION = 21,
        aiTextureType_UNKNOWN = 18,
    };

    enum class EAlphaMode
    {
        Opaque, // fully opaque
        Mask,   // alpha-tested against the cutoff (rendered opaque)
        Blend,  // alpha-blended
    };

    virtual const char* getName() const = 0;
    
    virtual glm::vec3 getBaseColor() const = 0;
    virtual glm::vec3 getEmissiveColor() const = 0;
    virtual glm::vec3 getSpecularColor() const = 0;
    virtual float getRoughnessFactor() const = 0;
    virtual float getMetalnessFactor() const = 0;
    virtual float getOpacity() const = 0;
    virtual EAlphaMode getAlphaMode() const = 0;
    virtual float getAlphaCutoff() const = 0;
    virtual float getEmissiveIntensity() const = 0;
    virtual float getRefractiveIndex() const = 0;

    virtual std::string getTexturePath(ETextureType type) const = 0;

    virtual uint32 getDiffuseTexIdx() const = 0;
    virtual uint32 getNormalTexIdx() const = 0;
    virtual uint32 getOpacityTexIdx() const = 0;
};