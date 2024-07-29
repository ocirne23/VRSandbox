module;

#include <cstddef> // offsetof

export module RendererVK.Layout;

import Core;
import Core.Frustum;
import RendererVK.VK;

export namespace RendererVKLayout
{
    export struct alignas(16) Ubo
    {
        glm::mat4 mvp;
        Frustum frustum;
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

        std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions(6);
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
        vertexInputAttributeDescriptions[2] = { // texcoords
            .location = 2,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(MeshVertex, texCoord),
        };
        vertexInputAttributeDescriptions[3] = { // inst_pos
            .location = 3,
            .binding = 1,
            .format = vk::Format::eR32G32B32Sfloat,
            .offset = offsetof(MeshTransform, pos),
        };
        vertexInputAttributeDescriptions[4] = { // inst_scale
            .location = 4,
            .binding = 1,
            .format = vk::Format::eR32Sfloat,
            .offset = offsetof(MeshTransform, scale),
        };
        vertexInputAttributeDescriptions[5] = { // inst_rot
            .location = 5,
            .binding = 1,
            .format = vk::Format::eR32G32B32A32Sfloat,
            .offset = offsetof(MeshTransform, quat),
        };
        return { vertexInputBindingDescriptions, vertexInputAttributeDescriptions };
    }
}