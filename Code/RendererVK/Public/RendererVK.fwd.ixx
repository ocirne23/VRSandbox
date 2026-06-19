export module RendererVK.fwd;

export extern "C++"
{
	class Renderer;
	class ObjectContainer;
	class RenderNode;
	struct PointLight;
    struct AreaLight;
    struct SpotLight;

	class Instance;
	class Device;
	class Surface;
	class SwapChain;
	class RenderPass;
	class CommandBuffer;
	class GraphicsPipeline;
	class StagingManager;
	class MeshData;
	class MeshInstance;
}