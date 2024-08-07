export module RendererVK.Layout;

import Core;
import Core.glm;
import Core.Frustum;
import RendererVK.VK;

export namespace RendererVKLayout
{
    export struct alignas(16) Ubo
    {
        glm::mat4 mvp;
        Frustum frustum;
        glm::vec3 viewPos;
    };

    export struct alignas(16) MeshTransform
    {
        glm::vec3 pos;
        float scale = 1.0f;
        glm::quat quat;
    };

    export struct alignas(16) MeshInstance
    {
        MeshTransform transform;
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

    export struct VertexLayoutInfo
    {
        std::vector<vk::VertexInputBindingDescription> bindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> attributeDescriptions;
    };

    VertexLayoutInfo getVertexLayoutInfo()
    {
        std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptions(2);
        vertexInputBindingDescriptions[0] = {
            .binding = 0,
            .stride = sizeof(MeshVertex),
            .inputRate = vk::VertexInputRate::eVertex,
        };
        vertexInputBindingDescriptions[1] = {
            .binding = 1,
            .stride = sizeof(MeshTransform),
            .inputRate = vk::VertexInputRate::eInstance,
        };

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions(7);
        vertexInputAttributeDescriptions[0] = { // position
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(MeshVertex, position),
        };
        vertexInputAttributeDescriptions[1] = { // normals
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(MeshVertex, normal),
        };
        vertexInputAttributeDescriptions[2] = { // tangents
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .offset = offsetof(MeshVertex, tangent),
        };
        vertexInputAttributeDescriptions[3] = { // texcoords
            .location = 3,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(MeshVertex, texCoord),
        };
        vertexInputAttributeDescriptions[4] = { // inst_pos
            .location = 4,
            .binding = 1,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(MeshTransform, pos),
        };
        vertexInputAttributeDescriptions[5] = { // inst_scale
            .location = 5,
            .binding = 1,
            .format = vk::Format::eR32Sfloat,
            .offset = offsetof(MeshTransform, scale),
        };
        vertexInputAttributeDescriptions[6] = { // inst_rot
            .location = 6,
            .binding = 1,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .offset = offsetof(MeshTransform, quat),
        };
        return { vertexInputBindingDescriptions, vertexInputAttributeDescriptions };
    }
}