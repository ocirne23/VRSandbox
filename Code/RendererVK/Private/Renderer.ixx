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
import :CompositePipeline;
import :EyeAdaptationPipeline;
import :GraphicsPipeline;
import :Light;
import :GpuCrashTracker;
import :Settings;

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
    void addFogVolume(const RendererVKLayout::FogVolumeInfo& fogVolume);
    void addPointLight(const PointLight& light);
    void addAreaLight(const AreaLight& areaLight);
    void addSpotLight(const SpotLight& spotLight);
    void setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
    // Flat, non-physical minimum ambient radiance (lights otherwise unlit areas; applied once at final shading).
    void setAmbientLight(const glm::vec3& color, float intensity) { m_skyParams.ambientColor = color; m_skyParams.ambientIntensity = intensity; }
    // Directional sky radiance (moonlight / space light), along the sky up axis, unshadowed.
    void setSkyRadiance(const glm::vec3& color, float intensity) { m_skyParams.skyRadianceColor = color; m_skyParams.skyRadianceIntensity = intensity; }

    void setSkyParams(const SkyParams& sky) { m_skyParams = sky; }
    void setFogParams(const FogParams& fog) { m_fogParams = fog; }
    void setPostParams(const PostParams& post) { m_postParams = post; setHaveToRecordCommandBuffers(); }
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
    void recordEyeAdaptation(uint32 frameIdx);
    void recordComposite(uint32 frameIdx);
    bool recordGlobalIllum(uint32 frameIdx); // returns true if the ray-tracing TLAS handle changed this frame
    void setHaveToRecordCommandBuffers();
    void recreateSwapchain();
    void createLightGridBuffers();
    void initImgui(Window& window);

    friend class ObjectContainer;
    void addObjectContainer(ObjectContainer* pObjectContainer);

    uint32 addRenderNodeTransform(const Transform& transform);
    uint32 addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos);
    uint32 addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos);
    uint32 addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets);

    // Capacity growth: each submits pending staged copies, waits for the GPU to go idle, recreates the
    // affected buffers at (at least) double the size, restores persistent contents, and forces a command
    // buffer re-record. Rare events, so the GPU stall is acceptable.
    void waitForGpuAndFlushStaging();
    void growRenderNodeCapacity(uint32 needed);
    void growMeshInstanceCapacity(uint32 needed);
    void growUniqueMeshCapacity(uint32 needed);
    void growMaterialCapacity(uint32 needed);
    void growInstanceOffsetCapacity(uint32 needed);
    void growLightGridBuffers(size_t neededGridBytes, uint32 neededTableEntries);
    // Reads last frame's light grid usage counters and grows the grid/table before they overflow.
    void checkLightGridCapacity();

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
    CompositePipeline m_compositePipeline;
    EyeAdaptationPipeline m_eyeAdaptationPipeline;
    AccelerationStructure m_accelStructure;
    GIProbePipeline m_giProbePipeline;

    bool  m_taaEnabled = true;
    float m_taaFeedback = 0.9f;
    uint32 m_taaJitterFrame = 0;

    uint32 m_blasBuiltCount = 0;
    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    glm::vec3 m_giPrevCameraPos = glm::vec3(0.0f); // last frame's camera; drives GI clipmap probe freshness
    uint32 m_frameCounter = 0; // monotonic; rotates the GI probe ray set each frame

    SkyParams m_skyParams;
    FogParams m_fogParams;
    PostParams m_postParams;
    uint32 m_fogVolumeCounter = 0;

    bool  m_rtSunShadow = true; // sun shadows from TLAS ray queries instead of PCSS cascades (A/B tweak)
    int   m_sunShadowRays = 5;   // RT sun shadow rays per pixel
    bool  m_rtLightShadows = true; // ray-traced shadows for punctual/area/tube lights
    bool  m_rtSkyRadiance = true;  // ray-traced sky visibility for the sky radiance light (GI probe trace)

    bool   m_giProbeDebugEnabled = false;
    uint32 m_giProbeDebugMode = 0;
    float  m_giProbeDebugRadius = 0.12f;

    glm::ivec2 m_windowSize;
    Rect m_viewportRect = Rect();
    bool m_windowMinimized = false;
    bool m_vsyncEnabled = true;

    // Live buffer capacities (element counts); grown on demand from the INITIAL_* seeds in Layout.ixx.
    uint32 m_maxRenderNodes = RendererVKLayout::INITIAL_RENDER_NODES;
    uint32 m_maxUniqueMeshes = RendererVKLayout::INITIAL_UNIQUE_MESHES;
    uint32 m_maxUniqueMaterials = RendererVKLayout::INITIAL_UNIQUE_MATERIALS;
    uint32 m_maxInstanceOffsets = RendererVKLayout::INITIAL_INSTANCE_OFFSETS;
    uint32 m_maxInstanceData = RendererVKLayout::INITIAL_INSTANCE_DATA;
    // Mesh-instance overflow detected mid-frame (possibly from worker threads): the overflowing nodes are
    // dropped for that frame and the capacity is grown at the next beginFrame (a safe sync point).
    uint32 m_pendingMaxInstanceData = 0;
    size_t m_lightGridBufferSize = RendererVKLayout::INITIAL_LIGHT_GRID_BUFFER_SIZE;
    uint32 m_lightTableEntries = RendererVKLayout::INITIAL_LIGHT_TABLE_NUM_ENTRIES;
    uint32 m_maxGiTlasInstances = RendererVKLayout::GI_INITIAL_TLAS_INSTANCES;
    // Texture-array descriptor sizing: the layouts declare the fixed device-limit cap (m_maxTextures,
    // set once at init from TextureManager::getDescriptorCap()); the sets are allocated with the live
    // variable count (m_numTextureDescriptors, re-synced from TextureManager on generation change).
    uint32 m_maxTextures = RendererVKLayout::INITIAL_TEXTURES;
    uint32 m_numTextureDescriptors = RendererVKLayout::INITIAL_TEXTURES;
    uint32 m_meshDataGeneration = 0; // last seen MeshDataManager::getGeneration(); change -> re-record
    uint32 m_textureGeneration = 0;  // last seen TextureManager::getGeneration(); change -> rebuild texture-array pipelines

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
        CommandBuffer eyeAdaptCommandBuffer;
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