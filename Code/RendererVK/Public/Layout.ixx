export module RendererVK.Layout;

import Core;
import Core.glm;
import Core.Frustum;

export namespace RendererVKLayout
{
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
        uint32 meshInfoIdx;
    };

    export struct MeshInfo
    {
        float radius = 1.0f;
        uint32 indexCount;
        uint32 firstIndex;
        int32  vertexOffset;
        uint32 firstInstance;
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