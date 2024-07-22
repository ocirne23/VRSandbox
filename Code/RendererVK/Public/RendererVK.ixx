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
import RendererVK.Texture;
import RendererVK.Sampler;
import RendererVK.StagingManager;
import RendererVK.MeshDataManager;

export class Mesh;
export class Window;
export class MeshData;

export struct InstanceData;

export class RendererVK final
{
public:

	constexpr static uint32 NUM_FRAMES_IN_FLIGHT = 2;

	RendererVK();
	~RendererVK();
	RendererVK(const RendererVK&) = delete;

	bool initialize(Window& window, bool enableValidationLayers);
	void update(double deltaSec, const glm::mat4& mvpMatrix);
	void render();
	void updateMeshSet(std::vector<Mesh>& meshData);

	const char* getDebugText();

private:

	void recordCommandBuffers();

private:

	Instance m_instance;
	Device m_device;
	Surface m_surface;
	SwapChain m_swapChain;
	RenderPass m_renderPass;
	Framebuffers m_framebuffers;
	Pipeline m_pipeline;
	Texture m_texture;
	Sampler m_sampler;
	StagingManager m_stagingManager;

	friend class Mesh;
	MeshDataManager m_meshDataManager;

	std::vector<Mesh>* m_pMeshSet = nullptr;

	struct PerFrameData
	{
		Buffer uniformBuffer;
		Buffer indirectCommandBuffer;
		Buffer instanceDataBuffer;

		void* mappedUniformBuffer = nullptr;
		vk::DrawIndexedIndirectCommand* mappedIndirectCommands = nullptr;
		InstanceData* mappedInstanceData = nullptr;
	};
	std::array<PerFrameData, NUM_FRAMES_IN_FLIGHT> m_perFrameData;
	glm::mat4 m_mvpMatrix;
};

export namespace VK
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
	RendererVK g_renderer;
#pragma warning(default: 4075)
}