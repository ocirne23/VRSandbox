module;

#include <cstddef>

module RendererVK.Mesh;

import Core;
import RendererVK.VK;
import RendererVK.Device;
import RendererVK.MeshDataManager;
import RendererVK.MeshInstance;
import RendererVK;

Mesh::Mesh() {}
Mesh::~Mesh() {}

Mesh::Mesh(Mesh&& copy)
{
	m_numIndices = copy.m_numIndices;
	m_vertexOffset = copy.m_vertexOffset;
	m_indexOffset = copy.m_indexOffset;
	m_instanceData = std::move(copy.m_instanceData);
	m_instances = std::move(copy.m_instances);
}

bool Mesh::initialize(MeshData& meshData)
{
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
	m_numIndices = (uint32)indices.size();

	MeshDataManager& meshDataManager = VK::g_renderer.m_meshDataManager;
	m_vertexOffset = (uint32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(VertexLayout)) / sizeof(VertexLayout));
	m_indexOffset  = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(IndexLayout)) / sizeof(IndexLayout));

	m_instances.shrink_to_fit();
	m_instances.reserve(1);

	return true;
}

VertexLayoutInfo Mesh::getVertexLayoutInfo()
{
	std::vector<vk::VertexInputBindingDescription> vertexInputBindingDescriptions(2);
	vertexInputBindingDescriptions[0] =	{
		.binding = 0,
		.stride = sizeof(VertexLayout),
		.inputRate = vk::VertexInputRate::eVertex,
	};
	vertexInputBindingDescriptions[1] = {
		.binding = 1,
		.stride = sizeof(InstanceData),
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
		.offset = offsetof(InstanceData, pos),
	};
	vertexInputAttributeDescriptions[4] = { // inst_scale
		.location = 4,
		.binding = 1,
		.format = vk::Format::eR32Sfloat,
		.offset = offsetof(InstanceData, scale),
	};
	vertexInputAttributeDescriptions[5] = { // inst_rot
		.location = 5,
		.binding = 1,
		.format = vk::Format::eR32G32B32A32Sfloat,
		.offset = offsetof(InstanceData, rot),
	};
	return { vertexInputBindingDescriptions, vertexInputAttributeDescriptions };
}

void Mesh::addInstance(MeshInstance* pInstance)
{
	size_t startCapacity = m_instances.capacity();
	pInstance->m_instanceDataIdx = (uint32)m_instances.size();
	pInstance->m_pMesh = this;
	m_instances.emplace_back(pInstance);
	m_instanceData.emplace_back();
}

void Mesh::removeInstance(MeshInstance* pInstance)
{
	const uint32 idx = pInstance->m_instanceDataIdx;
	if (idx == m_instances.size() - 1)
	{
		m_instances.pop_back();
		return;
	}
	m_instances[idx] = m_instances.back();
	m_instances.pop_back();
	m_instanceData.pop_back();
	m_instances[idx]->m_instanceDataIdx = idx;
}

void Mesh::setInstanceCapacity(uint32 capacity)
{
	assert(capacity >= m_instances.size());
	m_instances.reserve(capacity);
	m_instanceData.reserve(capacity);
}

void Mesh::setInstanceData(uint32 instanceIdx, InstanceData* pInstanceData)
{
	m_instanceData[instanceIdx] = *pInstanceData;
}