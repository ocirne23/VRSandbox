module RendererVK.CPUCullingRasterizer;

import Core;

bool CPUCullingRasterizer::initialize(glm::ivec2 size)
{
	m_pDepthBuffer = new float[size.x * size.y];
	return true;
}

bool CPUCullingRasterizer::addAndCullItem(const AABB& cullSize, const AABB& occludeSize)
{
	glm::vec4 cullMin = glm::vec4(cullSize.min, 1.0f);
	glm::vec4 cullMax = glm::vec4(cullSize.max, 1.0f);
	glm::vec4 cullMinClip = m_projectionMatrix * cullMin;
	glm::vec4 cullMaxClip = m_projectionMatrix * cullMax;

	glm::vec4 occludeMin = glm::vec4(occludeSize.min, 1.0f);
	glm::vec4 occludeMax = glm::vec4(occludeSize.max, 1.0f);
	glm::vec4 occludeMinClip = m_projectionMatrix * occludeMin;
	glm::vec4 occludeMaxClip = m_projectionMatrix * occludeMax;



	return true;
}