module;

#include "VK.h"
#include <glm/glm.hpp>

module RendererVK.RenderObject;

import RendererVK.Device;
import RendererVK.Buffer;
import RendererVK.CommandBuffer;
import RendererVK.StagingManager;

RenderObject::RenderObject() {}
RenderObject::~RenderObject() {}

bool RenderObject::initialize(const Device& device, StagingManager& stagingManager, MeshData& meshData)
{
	std::vector<VertexLayout> vertices;
	vertices.reserve(meshData.getNumVertices());
	glm::vec3* pVertices = meshData.getVertices();
	glm::vec3* pNormals = meshData.getNormals();
	glm::vec3* pTexCoords = meshData.getTexCoords();

	for (uint32_t i = 0; i < meshData.getNumVertices(); i++)
	{
		VertexLayout& vertex = vertices.emplace_back();
		vertex.position = pVertices[i];
		vertex.normal = pNormals[i];
		vertex.textCoord = glm::vec2(pTexCoords[i]);
	}
	std::vector<uint32_t> indices;
	meshData.getIndices(indices);
	m_numIndices = (uint32_t)indices.size();

	m_vertexBuffer.initialize(device, vertices.size() * sizeof(VertexLayout), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);
	m_indexBuffer.initialize(device, indices.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	stagingManager.upload(m_vertexBuffer.getBuffer(), vertices.size() * sizeof(VertexLayout), vertices.data());
	stagingManager.upload(m_indexBuffer.getBuffer(), indices.size() * sizeof(uint32_t), indices.data());
	/*
	Buffer vertexStagingBuffer;
	vertexStagingBuffer.initialize(device, vertices.size() * sizeof(VertexLayout), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	std::span<uint8_t> pVertexStagingMemory = vertexStagingBuffer.mapMemory();
	memcpy(pVertexStagingMemory.data(), vertices.data(), vertices.size() * sizeof(VertexLayout));
	vertexStagingBuffer.unmapMemory();

	Buffer indexStagingBuffer;
	indexStagingBuffer.initialize(device, indices.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	std::span<uint8_t> pIndexStagingMemory = indexStagingBuffer.mapMemory();
	memcpy(pIndexStagingMemory.data(), indices.data(), indices.size() * sizeof(uint32_t));
	indexStagingBuffer.unmapMemory();


	CommandBuffer commandBuffer;
	commandBuffer.initialize(device);
	vk::CommandBuffer vkCommandBuffer = commandBuffer.getCommandBuffer();
	vkCommandBuffer.begin({ .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	vk::BufferCopy vertexCopyRegion = { .size = vertices.size() * sizeof(VertexLayout) };
	vkCommandBuffer.copyBuffer(vertexStagingBuffer.getBuffer(), m_vertexBuffer.getBuffer(), 1, &vertexCopyRegion);
	vk::BufferCopy indexCopyRegion = { .size = indices.size() * sizeof(uint32_t) };
	vkCommandBuffer.copyBuffer(indexStagingBuffer.getBuffer(), m_indexBuffer.getBuffer(), 1, &indexCopyRegion);
	vkCommandBuffer.end();
	commandBuffer.submitGraphics();
	device.getGraphicsQueue().waitIdle();
	*/
	/*
	m_vertexBuffer.initialize(device, vertices.size() * sizeof(VertexLayout), vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	std::span<uint8_t> pVertexMemory = m_vertexBuffer.mapMemory();
	memcpy(pVertexMemory.data(), vertices.data(), vertices.size() * sizeof(VertexLayout));
	m_vertexBuffer.unmapMemory();

	m_indexBuffer.initialize(device, indices.size() * sizeof(uint32_t), vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
	std::span<uint8_t> pIndexMemory = m_indexBuffer.mapMemory();
	memcpy(pIndexMemory.data(), indices.data(), indices.size() * sizeof(uint32_t));
	m_indexBuffer.unmapMemory();
	*/
	return true;
}

VertexLayoutInfo RenderObject::getVertexLayoutInfo()
{
	vk::VertexInputBindingDescription vertexInputBindingDescription
	{
		.binding = 0,
		.stride = sizeof(VertexLayout),
		.inputRate = vk::VertexInputRate::eVertex,
	};
	std::vector<vk::VertexInputAttributeDescription> vertexInputAttributeDescriptions(3);
	vertexInputAttributeDescriptions[0] = {
		.location = 0,
		.binding = 0,
		.format = vk::Format::eR32G32B32Sfloat,
		.offset = 0,
	};
	vertexInputAttributeDescriptions[1] = {
		.location = 1,
		.binding = 0,
		.format = vk::Format::eR32G32B32Sfloat,
		.offset = 12,
	};
	vertexInputAttributeDescriptions[2] = {
		.location = 2,
		.binding = 0,
		.format = vk::Format::eR32G32Sfloat,
		.offset = 24,
	};
	return { vertexInputBindingDescription, vertexInputAttributeDescriptions };
}

