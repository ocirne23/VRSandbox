export module RendererVK:Renderer;

import Core;
import Core.glm;
import Core.Rect;
import Core.Transform;
import Core.Camera;

import :Layout;
import :Instance;
import :Device;
import :Surface;
import :SwapChain;
import :RenderPass;
import :Framebuffers;
import :CommandBuffer;
import :Buffer;
import :MeshDataManager;
import :DescriptorSet;
import :IndirectCullComputePipeline;
import :StaticMeshGraphicsPipeline;
import :LightGridComputePipeline;
import :ShadowMap;
import :ShadowCullComputePipeline;
import :ShadowMapGraphicsPipeline;
import :AccelerationStructure;
import :GIProbePipeline;
import :GBuffer;
import :GBufferPipeline;
import :RTAOPipeline;
import :Light;
import :GpuCrashTracker;

export import Core.fwd;

export class MeshInstance;
export class MeshData;
export class ObjectContainer;
export class RenderNode;
export struct Frustum;

export enum class EValidation { ENABLED, DISABLED };
export enum class EVSync { ENABLED, DISABLED };

export class Renderer final
{
public:

    Renderer() {}
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer(const Renderer&&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(const Renderer&&) = delete;

    bool initialize(Window& window, EValidation validation, EVSync vsync);

    const Frustum& beginFrame(const Camera& camera);
    void renderNodeThreadSafe(const RenderNode& node);
    void renderNode(const RenderNode& node);
    void addLightInfo(const RendererVKLayout::LightInfo& light);
    void addPointLight(const PointLight& light);
    void addAreaLight(const AreaLight& areaLight);
    void addSpotLight(const SpotLight& spotLight);
    void setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
    void setAmbientIntensity(float strength) { m_ambientIntensity = strength; } // 0.1 default
    void setGIIntensity(float strength) { m_giIntensity = strength; } // 1.0 default

    struct SkyParams
    {
        glm::vec3 zenith = glm::vec3(0.80f, 0.75f, 0.85f);
        glm::vec3 horizon = glm::vec3(0.80f, 0.55f, 0.40f);
        glm::vec3 ground = glm::vec3(0.0f);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f); // sky-up axis; set to the local up on a planet
        float intensity = 1.0f;
        float sunAngularCos = 1.0f;// 0.9999f; // ~0.8 deg disc; 1.0 disables the disc
        float sunGlow = 0.0f;    // 0 = none; e.g. 256 for a tight bloom around the sun
    };
    void setSkyParams(const SkyParams& sky) { m_skyParams = sky; }
    void present();

    uint32 getNumMeshInstances() const { return m_meshInstanceCounter; }
    uint32 getNumRenderNodes() const { return (uint32)m_renderNodeTransforms.size(); }
    uint32 getNumMeshTypes() const { return m_meshInfoCounter; }
    uint32 getNumMaterials() const { return m_materialInfoCounter; }
    uint32 getCurrentFrameIndex() const { return m_swapChain.getCurrentFrameIndex(); }

    void reloadShaders();

    // GI probe debug visualization: instanced cubes at every live probe cell.
    void toggleGiProbeDebug() { m_giProbeDebugEnabled = !m_giProbeDebugEnabled; }
    void cycleGiProbeDebugMode() { m_giProbeDebugMode ^= 1u; } // 0 = irradiance, 1 = cellSize/LOD

    void setWindowMinimized(bool minimized);
    void recreateWindowSurface(Window& window);
    void setViewportRect(const Rect& rect) { if (rect != m_viewportRect) { m_viewportRect = rect; setHaveToRecordCommandBuffers(); } }

    struct Stats
    {
        uint32 numLights;
        uint32 maxLights;

		uint32 numMeshInstances;
		uint32 maxMeshInstances;

        uint32 numInstanceOffsets;
		uint32 maxInstanceOffsets;

        uint32 numMeshTypes;
		uint32 maxMeshTypes;

        uint32 numMaterials;
        uint32 maxMaterials;

		uint32 numRenderNodes;
        uint32 maxRenderNodes;

        uint32 numTextures;
        uint32 maxTextures;

        uint64 vertexDataUsedBytes;
		uint64 maxVertexDataBytes;

		uint64 indexDataUsedBytes;
		uint64 maxIndexDataBytes;

        uint32 numObjectContainers;

        uint32 numLightGrids;
		uint32 maxLightGrids;

		uint64 lightGridMemUsageBytes;
		uint64 maxLightGridMemUsageBytes;
    };
    Stats getStats();

private:

    CommandBuffer& getCurrentCommandBuffer() { return m_perFrameData[m_swapChain.getCurrentFrameIndex()].primaryCommandBuffer; }

    void recordCommandBuffers();
    void setHaveToRecordCommandBuffers();
    void recreateSwapchain();
    void initImgui(Window& window);

    friend class ObjectContainer;
    void addObjectContainer(ObjectContainer* pObjectContainer);

    uint32 addRenderNodeTransform(const Transform& transform);
    uint32 addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos);
    uint32 addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos);
    uint32 addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets);

    friend class RenderNode;
    inline Transform& getRenderNodeTransform(uint32 idx) { return m_renderNodeTransforms[idx]; }

private:

    Instance m_instance;
    Device m_device;
    Surface m_surface;
    SwapChain m_swapChain;
    RenderPass m_renderPass;
    Framebuffers m_framebuffers;
	GpuCrashTracker m_gpuCrashTracker;

    IndirectCullComputePipeline m_indirectCullComputePipeline;
    LightGridComputePipeline m_lightGridComputePipeline;
    StaticMeshGraphicsPipeline m_staticMeshGraphicsPipeline;

    // Depth + world-normal prepass (G-buffer), one per frame-in-flight (written then sampled within the
    // same frame, like the shadow maps). Drives the screen-space ray-traced AO denoise pipeline.
    std::array<GBuffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_gbuffers;
    GBufferPipeline m_gbufferPipeline;

    // Screen-space ray-traced AO (half-res compute) feeding a temporal-reprojection denoise.
    RTAOPipeline m_rtaoPipeline;

    // Hardware ray-traced diffuse GI. The acceleration structures are queried by the probe-trace pass;
    // BLASes are built once per mesh, the TLAS rebuilt each frame from the instance list.
    AccelerationStructure m_accelStructure;
    GIProbePipeline m_giProbePipeline;
    std::vector<RendererVKLayout::MeshInfo> m_cpuMeshInfos; // CPU copy kept for BLAS builds
    uint32 m_blasBuiltCount = 0;
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    uint32 m_frameCounter = 0; // monotonic; rotates the GI probe ray set each frame

    // One shadow map per frame-in-flight: it is written early and sampled later in the same frame,
    // so a single shared image would race across the 2 frames in flight (guarded only by the
    // per-frame fence). Indexing by frameIdx makes it safe like the other per-frame resources.
    std::array<ShadowMap, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_shadowMaps;
    ShadowCullComputePipeline m_shadowCullComputePipeline;
    ShadowMapGraphicsPipeline m_shadowMapGraphicsPipeline;
    glm::vec3 m_sunDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.5f));
    glm::vec3 m_sunColor = glm::vec3(1.0f);
    float m_sunIntensity = 5.0f;
    SkyParams m_skyParams;
    float m_ambientIntensity = 0.1f;
    float m_giIntensity = 1.0f;

    std::vector<ObjectContainer*> m_objectContainers;
    std::vector<Transform> m_renderNodeTransforms;
    std::vector<uint32> m_numInstancesPerMesh;
    std::vector<uint32> m_freeRenderNodeIndexes;

    uint32 m_meshInfoCounter = 0;
    uint32 m_materialInfoCounter = 0;
    uint32 m_instanceOffsetCounter = 0;
    uint32 m_meshInstanceCounter = 0;
    uint32 m_lightCounter = 0;

    Buffer m_meshInfosBuffer;
    Buffer m_materialInfosBuffer;
    Buffer m_instanceOffsetsBuffer;

    struct PerFrameData
    {
        DescriptorSet staticMeshPipelineDescriptorSet;
        DescriptorSet gbufferDescriptorSet;
        DescriptorSet indirectCullPipelineDescriptorSet;
        DescriptorSet lightGridPipelineDescriptorSet;
        DescriptorSet shadowCullDescriptorSet;
        DescriptorSet shadowDrawDescriptorSet;

        CommandBuffer primaryCommandBuffer;
        CommandBuffer staticMeshCommandBuffer;
        CommandBuffer gbufferCommandBuffer;
        CommandBuffer aoCommandBuffer;
        CommandBuffer indirectCullCommandBuffer;
        CommandBuffer lightGridCommandBuffer;
        CommandBuffer imguiCommandBuffer;
        CommandBuffer shadowCullCommandBuffer;
        CommandBuffer shadowDrawCommandBuffer;
        CommandBuffer globalIllumCommandBuffer;
        CommandBuffer giProbeDebugCommandBuffer;

        bool updated = false;
        Buffer ubo;
        Buffer inRenderNodeTransformsBuffer;
        Buffer inMeshInstancesBuffer;
        Buffer inFirstInstancesBuffer;

        Buffer lightInfosBuffer;
        Buffer lightGridsBuffer;
        Buffer lightTableBuffer;

        RendererVKLayout::Ubo* mappedUniformBuffer = nullptr;
        std::span<RendererVKLayout::RenderNodeTransform> mappedRenderNodeTransforms;
        std::span<RendererVKLayout::InMeshInstance> mappedMeshInstances;
        std::span<uint32> mappedFirstInstances;

        std::span<RendererVKLayout::LightInfo> mappedLightInfos;
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;

    glm::ivec2 m_windowSize;
    Rect m_viewportRect = Rect();
    bool m_windowMinimized = false;
    bool m_vsyncEnabled = true;

    bool   m_giProbeDebugEnabled = false;
    uint32 m_giProbeDebugMode = 0;
    float  m_giProbeDebugRadius = 0.12f;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    Renderer rendererVK;
#pragma warning(default: 4075)
} // namespace Globals