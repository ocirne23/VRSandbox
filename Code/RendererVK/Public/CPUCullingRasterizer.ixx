export module RendererVK.CPUCullingRasterizer;

import Core;
import Core.AABB;

export class CPUCullingRasterizer final
{
public:
	CPUCullingRasterizer() {}
	~CPUCullingRasterizer() {}
	CPUCullingRasterizer(const CPUCullingRasterizer&) = delete;

	bool initialize(glm::ivec2 size);
	void setProjectionMatrix(const glm::mat4& projectionMatrix) { m_projectionMatrix = projectionMatrix; }
	bool addAndCullItem(const AABB& cullSize, const AABB& occludeSize);

private:

	glm::mat4 m_projectionMatrix;
	float* m_pDepthBuffer = nullptr;
};