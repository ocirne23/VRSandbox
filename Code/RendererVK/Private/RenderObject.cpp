module;

#include <cstddef>

module RendererVK.RenderObject;

import Core;
import RendererVK.VK;
import RendererVK.Device;
import RendererVK.MeshDataManager;

RenderObject::RenderObject() {}
RenderObject::~RenderObject() {}

bool RenderObject::initialize(MeshDataManager& meshDataManager, MeshData& meshData)
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

	m_vertexOffset = (uint32)(meshDataManager.uploadVertexData(vertices.data(), vertices.size() * sizeof(VertexLayout)) / sizeof(VertexLayout));
	m_indexOffset  = (uint32)(meshDataManager.uploadIndexData(indices.data(), indices.size() * sizeof(IndexLayout)) / sizeof(IndexLayout));

	return true;
}

VertexLayoutInfo RenderObject::getVertexLayoutInfo()
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

