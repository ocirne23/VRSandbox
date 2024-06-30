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

export class Window;

export class RendererVK final
{
public:

	RendererVK();
	~RendererVK();
	RendererVK(const RendererVK&) = delete;

	bool initialize(Window& window, bool enableValidationLayers);

	void update(const glm::mat4& mvpMatrix);
	void render();

private:

	Instance m_instance;
	Device m_device;
	Surface m_surface;
	SwapChain m_swapChain;
	RenderPass m_renderPass;
	Framebuffers m_framebuffers;
	Pipeline m_pipeline;
	Buffer m_uniformBuffer;
	RenderObject m_renderObject;
	Texture m_texture;
	Sampler m_sampler;

	StagingManager m_stagingManager;
	Buffer m_indexedIndirectBuffer;

	glm::mat4 m_mvpMatrix;
};