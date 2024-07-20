export module RendererVK;

import Core;

import RendererVK.VK;
import RendererVK.Instance;
import RendererVK.Device;
import RendererVK.Surface;
import RendererVK.SwapChain;
import RendererVK.RenderPass;
import RendererVK.Framebuffers;
import RendererVK.Pipeline;
import RendererVK.CommandBuffer;
import RendererVK.Buffer;
import RendererVK.RenderObject;
import RendererVK.Texture;
import RendererVK.Sampler;
import RendererVK.StagingManager;
import RendererVK.MeshDataManager;

export class Window;

export class RendererVK final
{
public:

	RendererVK();
	~RendererVK();
	RendererVK(const RendererVK&) = delete;

	bool initialize(Window& window, bool enableValidationLayers);

	void recordCommandBuffers();
	void update(double deltaSec, const glm::mat4& mvpMatrix);
	void render();

	const char* getDebugText();

private:

	Instance m_instance;
	Device m_device;
	Surface m_surface;
	SwapChain m_swapChain;
	RenderPass m_renderPass;
	Framebuffers m_framebuffers;
	Pipeline m_pipeline;
	RenderObject m_renderObject;
	RenderObject m_renderObject2;
	Texture m_texture;
	Sampler m_sampler;

	StagingManager m_stagingManager;
	MeshDataManager m_meshDataManager;

	std::vector<Buffer> m_uniformBuffers;
	std::vector<Buffer> m_indirectCommandBuffers;
	std::vector<Buffer> m_instanceDataBuffers;

	glm::mat4 m_mvpMatrix;
};