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
import :VolumetricFogPipeline;
import :SceneColor;
import :TaaPipeline;
import :GraphicsPipeline;
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
    // Local participating-media box for the volumetric fog, submitted per frame like lights.
    void addFogVolume(const RendererVKLayout::FogVolumeInfo& fogVolume);
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
        float sunAngularCos = 0.99998f;// 0.9999f; // ~0.8 deg disc; 1.0 disables the disc
        float sunGlow = 0.5f; // sun halo strength (0 = none, ~0.5 subtle, 2 = heavy); HG forward lobe in sky.fs
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
    // The debug draw is now recorded once with the scene (the mode is baked into its push constant), so a mode
    // change must force a re-record. Enable/disable is handled per-frame in the primary, so only this needs it.
    void cycleGiProbeDebugMode() { m_giProbeDebugMode ^= 1u; setHaveToRecordCommandBuffers(); } // 0 = irradiance, 1 = cellSize/LOD

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
    // Per-pass secondary command-buffer recording (each fetches its per-frame data from frameIdx). The scene
    // passes are recorded once (gated by recordScene in recordCommandBuffers); recordGlobalIllum runs every
    // frame (it refits the ray-tracing TLAS and rotates the probe ray set).
    void recordIndirectCull(uint32 frameIdx);
    void recordLightGrid(uint32 frameIdx);
    void recordShadowCull(uint32 frameIdx);
    void recordShadowDraw(uint32 frameIdx);
    void recordStaticMesh(uint32 frameIdx);
    void recordGBuffer(uint32 frameIdx);
    void recordGiProbeDebug(uint32 frameIdx);
    void recordAO(uint32 frameIdx);
    void recordVolumetricFog(uint32 frameIdx);
    void recordFogApply(uint32 frameIdx);
    void recordTaa(uint32 frameIdx);
    void recordComposite(uint32 frameIdx);
    bool recordGlobalIllum(uint32 frameIdx); // returns true if the ray-tracing TLAS handle changed this frame
    void setHaveToRecordCommandBuffers();
    void recreateSwapchain();
    void initImgui(Window& window);
    void initComposite();

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
    GBufferPipeline m_gbufferPipeline;
    RTAOPipeline m_rtaoPipeline;
    VolumetricFogPipeline m_volumetricFogPipeline;
    TaaPipeline m_taaPipeline;
    ShadowCullComputePipeline m_shadowCullComputePipeline;
    ShadowMapGraphicsPipeline m_shadowMapGraphicsPipeline;
    GraphicsPipeline m_compositePipeline;
    AccelerationStructure m_accelStructure;
    GIProbePipeline m_giProbePipeline;

    bool  m_taaEnabled = true;
    float m_taaFeedback = 0.9f;
    uint32 m_taaJitterFrame = 0;

    uint32 m_blasBuiltCount = 0;
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    glm::vec3 m_giPrevCameraPos = glm::vec3(0.0f); // last frame's camera; drives GI clipmap probe freshness
    uint32 m_frameCounter = 0; // monotonic; rotates the GI probe ray set each frame

    glm::vec3 m_sunDirection = glm::normalize(glm::vec3(-0.5f, 1.0f, 0.1f));
    glm::vec3 m_sunColor = glm::vec3(0.9568f, 1.0f, 0.9214f);
    float m_sunIntensity = 3.0f;
    SkyParams m_skyParams;
    float m_cloudCoverage  = 0.45f;
    float m_cloudHeight    = 1800.0f; // meters above the viewer (slab base)
    float m_cloudThickness = 900.0f;  // slab thickness (m)
    float m_cloudScale     = 1.0f;    // multiplier on the base noise frequency
    float m_cloudWindSpeed = 1.0f;    // multiplier on the base wind drift
    float m_cloudWindAngle = 0.4f;    // radians
    float m_cloudSoftness  = 0.32f;   // density smoothstep width (small = crisp edges)
    float m_cloudDensity   = 1.5f;    // extinction strength (high = opaque cores)
    float m_cloudSharpness = 0.6f;    // density remap contrast (high = hard-edged shapes)
    float m_cloudBaseVar   = 0.6f;    // per-column base/top height variation (0 = flat slab)
    float m_cloudShading   = 2.0f;    // directional sun-shading strength
    float m_cloudSilver    = 0.8f;    // silver-lining strength
    float m_cloudAmbient   = 0.35f;   // sky-ambient amount in the cloud color
    float m_skyScatterBoost = 4.0f;   // sun color -> atmosphere scattering source strength
    float m_skyMieG        = 0.76f;   // Mie anisotropy (forward-scatter lobe)
    float m_sunDiscFeather = 0.2f;    // disc rim feather, fraction of the disc's angular size
    float m_starDensity    = 0.5f;
    float m_moonBrightness = 0.4f;
    float m_ambientIntensity = 0.1f;
    float m_giIntensity = 1.0f;

    // Volumetric fog (froxel grid; see VolumetricFogPipeline). All UBO-driven, so tweaks apply live.
    bool  m_fogEnabled = true;
    float m_fogDensity = 0.008f;        // global extinction at the height base (1/m)
    float m_fogHeightBase = 0.0f;       // world height where the global fog is densest
    float m_fogHeightFalloff = 0.02f;   // exponential density falloff above the base (1/m)
    glm::vec3 m_fogAlbedo = glm::vec3(0.85f, 0.87f, 0.9f);
    float m_fogAnisotropy = 0.5f;       // HG phase g (0 = isotropic, ->1 = forward scattering)
    float m_fogRange = 128.0f;          // froxel grid far distance (m)
    float m_fogNoiseScale = 0.08f;      // density noise frequency (1/m)
    float m_fogNoiseStrength = 0.5f;    // 0 = uniform fog, 1 = fully modulated (dusty wisps)
    float m_fogWindSpeed = 1.5f;        // noise drift (m/s)
    float m_fogTemporal = 0.85f;        // history blend weight (jittered Z integration)
    float m_fogSunBoost = 1.0f;
    float m_fogAmbientBoost = 1.0f;
    bool  m_fogLightShadows = false;    // shadow ray per froxel per grid light (expensive)
    uint32 m_fogVolumeCounter = 0;

    float m_shadowDepthBias = 0.000f;
    float m_shadowNormalBias = 0.0f;
    bool  m_rtSunShadow = true; // sun shadows from TLAS ray queries instead of PCSS cascades (A/B tweak)
    int   m_sunShadowRays = 5;   // RT sun shadow rays per pixel
    bool  m_rtLightShadows = true; // ray-traced shadows for punctual/area/tube lights

    bool   m_giProbeDebugEnabled = false;
    uint32 m_giProbeDebugMode = 0;
    float  m_giProbeDebugRadius = 0.12f;

    glm::ivec2 m_windowSize;
    Rect m_viewportRect = Rect();
    bool m_windowMinimized = false;
    bool m_vsyncEnabled = true;

    std::vector<RendererVKLayout::MeshInfo> m_cpuMeshInfos; // CPU copy kept for BLAS builds, refactor later

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
        SceneColor sceneColor;
        GBuffer gbuffer;
        ShadowMap shadowMap;

        DescriptorSet staticMeshPipelineDescriptorSet;
        DescriptorSet gbufferDescriptorSet;
        DescriptorSet compositeDescriptorSet;
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
        CommandBuffer volumetricFogCommandBuffer;
        CommandBuffer fogApplyCommandBuffer;
        CommandBuffer giProbeDebugCommandBuffer;
        CommandBuffer taaCommandBuffer;
        CommandBuffer compositeCommandBuffer;

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

        Buffer fogVolumesBuffer;
        std::span<RendererVKLayout::FogVolumes> mappedFogVolumes; // single element: count header + array
    };
    std::array<PerFrameData, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_perFrameData;
};

export namespace Globals
{
#pragma warning(disable: 4075)
#pragma init_seg(".CRT$XCU3")
    Renderer rendererVK;
#pragma warning(default: 4075)
} // namespace Globals