export module RendererVK.Layout;

import Core;
import Core.glm;
import Core.Frustum;

export namespace RendererVKLayout
{
    export constexpr uint32 NUM_FRAMES_IN_FLIGHT = 2;

    // TODO make these dynamic
    export constexpr uint32 MAX_UNIQUE_MESHES = 100;
    export constexpr uint32 MAX_INSTANCE_DATA = 1024 * 1024;
    export constexpr uint32 MAX_UNIQUE_MATERIALS = 100;

    static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
    static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

    export struct alignas(16) Ubo
    {
        glm::mat4 mvp;
        Frustum frustum;
        glm::vec3 viewPos;
    };

    export struct alignas(16) MeshInstance
    {
        glm::vec3 pos;
        float scale = 1.0f;
        glm::quat quat;
        uint16 meshInfoIdx;
        uint16 materialInfoIdx;
    };

    export struct alignas(16) MeshInfo
    {
        glm::vec3 center;
        float radius = 1.0f;
        uint32 indexCount;
        uint32 firstIndex;
        int32  vertexOffset;
        uint32 firstInstance;
    };

    export struct alignas(16) MaterialInfo
    {
        glm::vec3 baseColor;
        float roughness;
        glm::vec3 specularColor;
        float metalness;
        glm::vec3 emissiveColor;
        uint32 flags;
    };

    export struct MeshVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;
    };

    using MeshIndex = uint32;
}