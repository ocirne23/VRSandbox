module RendererVK.Mesh;

import Core;
import File.MeshData;
import RendererVK.Buffer;
import RendererVK.Device;
import RendererVK.MeshDataManager;
import RendererVK.MeshInstance;
import RendererVK.Layout;
import RendererVK;

Mesh::Mesh() {}
Mesh::~Mesh() {}

Mesh::Mesh(Mesh&& move)
{
    m_info = move.m_info;
    m_meshIdx = move.m_meshIdx;
    m_numInstances = move.m_numInstances;
}

bool Mesh::initialize(MeshData& meshData)
{
    std::vector<RendererVKLayout::MeshVertex> vertices;
    vertices.resize(meshData.getNumVertices());
    glm::vec3* pVertices = meshData.getVertices();
    glm::vec3* pNormals = meshData.getNormals();
    glm::vec3* pTexCoords = meshData.getTexCoords();
    glm::vec3* pTangents = meshData.getTangents();
    glm::vec3* pBitangents = meshData.getBitangents();

    for (uint32 i = 0; i < meshData.getNumVertices(); i++)
    {
        vertices[i].position = pVertices[i];
        vertices[i].normal = pNormals[i];
        const float handedness = glm::dot(pNormals[i], glm::cross(pTangents[i], pBitangents[i])) >= 0.0f ? 1.0f : -1.0f;
        vertices[i].tangent = glm::vec4(pTangents[i], handedness);
        vertices[i].texCoord = glm::vec2(pTexCoords[i]);
    }
    std::vector<uint32> indices;
    meshData.getIndices(indices);
    m_info.indexCount = (uint32)indices.size();

    MeshDataManager& meshDataManager = VK::g_renderer.m_meshDataManager;
    m_info.vertexOffset = (int32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(RendererVKLayout::MeshVertex)) / sizeof(RendererVKLayout::MeshVertex));
    m_info.firstIndex   = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(RendererVKLayout::MeshIndex)) / sizeof(RendererVKLayout::MeshIndex));
    m_info.radius       = meshData.getAABB().getRadius();
    return true;
}

void Mesh::addInstance(MeshInstance* pInstance)
{
    m_numInstances++;
    pInstance->meshInfoIdx = m_meshIdx;
}

void Mesh::removeInstance(MeshInstance* pInstance)
{
    assert(pInstance->meshInfoIdx == m_meshIdx);
    m_numInstances--;
}