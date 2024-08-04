module RendererVK.Mesh;

import Core;
import File.MeshData;
import RendererVK.VK;
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

bool Mesh::initialize(MeshData& meshData, uint32 meshIdx)
{
    m_meshIdx = meshIdx;

    std::vector<RendererVKLayout::MeshVertex> vertices;
    vertices.reserve(meshData.getNumVertices());
    glm::vec3* pVertices = meshData.getVertices();
    glm::vec3* pNormals = meshData.getNormals();
    glm::vec3* pTexCoords = meshData.getTexCoords();

    for (uint32 i = 0; i < meshData.getNumVertices(); i++)
    {
        RendererVKLayout::MeshVertex& vertex = vertices.emplace_back();
        vertex.position = pVertices[i];
        vertex.normal = pNormals[i];
        vertex.texCoord = glm::vec2(pTexCoords[i]);
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