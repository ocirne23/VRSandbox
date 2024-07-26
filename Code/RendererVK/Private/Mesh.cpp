module;

#include <cstddef>

module RendererVK.Mesh;

import Core;
import RendererVK.VK;
import RendererVK.Buffer;
import RendererVK.Device;
import RendererVK.MeshDataManager;
import RendererVK.MeshInstance;
import RendererVK;

Mesh::Mesh() {}
Mesh::~Mesh() {}

Mesh::Mesh(Mesh&& move)
{
    m_info = move.m_info;
    m_meshIdx = move.m_meshIdx;
    m_numInstances = move.m_numInstances;
}

bool Mesh::initialize(MeshData& meshData, uint32 meshIdx)
{
    m_meshIdx = meshIdx;

    std::vector<VertexLayout> vertices;
    vertices.reserve(meshData.getNumVertices());
    glm::vec3* pVertices = meshData.getVertices();
    glm::vec3* pNormals = meshData.getNormals();
    glm::vec3* pTexCoords = meshData.getTexCoords();

    for (uint32 i = 0; i < meshData.getNumVertices(); i++)
    {
        VertexLayout& vertex = vertices.emplace_back();
        vertex.position = pVertices[i];
        vertex.normal = pNormals[i];
        vertex.texCoord = glm::vec2(pTexCoords[i]);
    }
    std::vector<uint32> indices;
    meshData.getIndices(indices);
    m_info.indexCount = (uint32)indices.size();

    MeshDataManager& meshDataManager = VK::g_renderer.m_meshDataManager;
    m_info.vertexOffset = (int32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(VertexLayout)) / sizeof(VertexLayout));
    m_info.firstIndex   = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(IndexLayout)) / sizeof(IndexLayout));

    return true;
}

VertexLayoutInfo Mesh::getVertexLayoutInfo()
{
    std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptions(2);
    vertexInputBindingDescriptions[0] = {
        .binding = 0,
        .stride = sizeof(VertexLayout),
        .inputRate = vk::VertexInputRate::eVertex,
    };
    vertexInputBindingDescriptions[1] = {
        .binding = 1,
        .stride = sizeof(MeshInstance::RenderLayout),
        .inputRate = vk::VertexInputRate::eInstance,
    };

    std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions(6);
    vertexInputAttributeDescriptions[0] = { // position
        .location = 0,
        .binding = 0,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(VertexLayout, position),
    };
    vertexInputAttributeDescriptions[1] = { // normals
        .location = 1,
        .binding = 0,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(VertexLayout, normal),
    };
    vertexInputAttributeDescriptions[2] = { // texcoords
        .location = 2,
        .binding = 0,
        .format = vk::Format::eR32G32Sfloat,
        .offset = offsetof(VertexLayout, texCoord),
    };
    vertexInputAttributeDescriptions[3] = { // inst_pos
        .location = 3,
        .binding = 1,
        .format = vk::Format::eR32G32B32Sfloat,
        .offset = offsetof(MeshInstance::RenderLayout, pos),
    };
    vertexInputAttributeDescriptions[4] = { // inst_scale
        .location = 4,
        .binding = 1,
        .format = vk::Format::eR32Sfloat,
        .offset = offsetof(MeshInstance::RenderLayout, scale),
    };
    vertexInputAttributeDescriptions[5] = { // inst_rot
        .location = 5,
        .binding = 1,
        .format = vk::Format::eR32G32B32A32Sfloat,
        .offset = offsetof(MeshInstance::RenderLayout, rot),
    };
    return { vertexInputBindingDescriptions, vertexInputAttributeDescriptions };
}

void Mesh::addInstance(MeshInstance* pInstance)
{
    m_numInstances++;
    pInstance->meshIdx = m_meshIdx;
}

void Mesh::removeInstance(MeshInstance* pInstance)
{
    assert(pInstance->meshIdx == m_meshIdx);
    m_numInstances--;
}