export module RendererVK.Layout;

import Core;
import Core.glm;
import Core.Frustum;
import RendererVK.Transform;

export namespace RendererVKLayout
{
    constexpr uint32 NUM_FRAMES_IN_FLIGHT = 2;

    // TODO make these dynamic
    constexpr uint32 MAX_RENDER_NODES = 1024 * 32;
    constexpr uint32 MAX_UNIQUE_MESHES = 100;
    constexpr uint32 MAX_INSTANCE_OFFSETS = 1024;
    constexpr uint32 MAX_INSTANCE_DATA = 1024 * 1024;
    constexpr uint32 MAX_UNIQUE_MATERIALS = 100;

    static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
    static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

    struct alignas(16) Ubo
    {
        glm::mat4 mvp;
        Frustum frustum;
        glm::vec3 viewPos;
    private:
        uint32 _padding;
    };

    struct alignas(16) RenderNodeTransform : Transform {};
    struct alignas(16) MeshInstanceOffset {
        Transform transform;
    };

    struct alignas(16) InMeshInstance
    {
        uint32 renderNodeIdx;
        uint32 instanceOffsetIdx;
        uint16 meshIdx;
        uint16 materialIdx;
    private:
        uint32 _padding;
    };

    struct alignas(16) OutMeshInstance
    {
        Transform transform;
        uint32 meshIdxMaterialIdx;
    private:
        uint32 _padding[3];
    };

    struct alignas(16) MeshInfo
    {
        glm::vec3 center;
        float radius = 1.0f;
        uint32 indexCount;
        uint32 firstIndex;
        int32  vertexOffset;
        uint32 firstInstance;
    };

    struct alignas(16) MaterialInfo
    {
        glm::vec3 baseColor;
        float roughness;
        glm::vec3 specularColor;
        float metalness;
        glm::vec3 emissiveColor;
        uint32 flags;
    };

    struct MeshVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;
    };

    using MeshIndex = uint32;
}