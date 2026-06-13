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
    void addFogVolume(const RendererVKLayout::FogVolumeInfo& fogVolume);
    void addPointLight(const PointLight& light);
    void addAreaLight(const AreaLight& areaLight);
    void addSpotLight(const SpotLight& spotLight);
    void setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
    // Flat, non-physical minimum ambient radiance (lights otherwise unlit areas; applied once at final shading).
    void setAmbientLight(const glm::vec3& color, float intensity) { m_skyParams.ambientColor = color; m_skyParams.ambientIntensity = intensity; }
    // Directional sky radiance (moonlight / space light), along the sky up axis, unshadowed.
    void setSkyRadiance(const glm::vec3& color, float intensity) { m_skyParams.skyRadianceColor = color; m_skyParams.skyRadianceIntensity = intensity; }

    // Everything in the TweakPanel's "Sky" categories (Sky / Sky/Sun / Sky/Atmosphere / Sky/Clouds /
    // Sky/Stars / Sky/Nebula / Sky/Moon).
    struct SkyParams
    {
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f); // sky "up" axis; also the sky radiance light direction

        // Sun
        glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.4f, 0.91f, 0.09f));
        glm::vec3 sunColor = glm::vec3(0.9568f, 1.0f, 0.9214f);
        float sunIntensity = 3.0f;
        float sunAngularCos = 0.99998f;     // cos of the disc radius (1 = disc off)
        float sunGlow = 1.0f;               // sun halo strength (0 = none, ~0.5 subtle, 2 = heavy); HG forward lobe in sky.fs
        float sunRolloff = 1.25f;           // sky highlight roll-off: soft-clips the overexposed sun region so its gradient survives (0 = raw hard clip)
        float sunRolloffKnee = 0.75f;       // luminance where compression starts at full roll-off (lower = more range compressed)
        float sunRolloffHeadroom = 6.0f;    // brightness range the shoulder absorbs (higher = brighter values stay distinguishable)
        float shadowDepthBias = 0.0f;       // sun cascade depth bias
        float shadowNormalBias = 0.0f;      // sun cascade normal bias (texels)

        // Ambient + sky radiance (the non-sun lighting inputs)
        glm::vec3 ambientColor = glm::vec3(1.0f);   // flat non-physical minimum ambient
        float ambientIntensity = 0.005f;
        glm::vec3 skyRadianceColor = glm::vec3(0.55f, 0.65f, 1.0f); // directional sky radiance (moonlight/space light), along up
        float skyRadianceIntensity = 0.20f;

        // Ground plane of the sky sphere (below the horizon): albedo lit by sun + sky. Also tints the
        // ground-bounce fallback in skyRadiance() for downward GI/fog rays.
        glm::vec3 groundColor = glm::vec3(0.0f, 0.17f, 1.0f);
        float groundIntensity = 1.0f;

        // Atmosphere scattering: multipliers on the Earth sea-level Rayleigh/Mie coefficients. These drive
        // both the visible sky and (through skyRadiance) the majority of the indirect sky lighting.
        float rayleighScatter = 1.0f;
        float mieScatter = 1.0f;
        float scatterBoost = 3.0f;          // in-scatter multiplier: how much sunlight the atmosphere scatters (more = more indirect light)
        float mieG = 0.76f;                 // Mie anisotropy (forward-scatter lobe)
        float rayleighHeight = 8500.0f;     // Rayleigh scale height (m): how fast air density falls off
        float mieHeight = 1200.0f;          // Mie scale height (m): how high the haze layer reaches
        float mieExtinction = 1.11f;        // Mie extinction/scattering ratio (> 1 = absorbing haze)
        float ozone = 1.0f;                 // ozone absorption strength (1 = Earth-like); absorbs green/yellow,
                                            // suppresses the green horizon band single scattering produces

        // Clouds
        float cloudCoverage = 0.45f;
        float cloudHeight = 6300.0f;        // meters above the viewer (slab base)
        float cloudThickness = 86.0f;    // slab thickness sqrt(m)
        float cloudScale = 0.7f;            // multiplier on the base noise frequency
        float cloudWindSpeed = 3.00f;       // multiplier on the base wind drift
        float cloudWindAngle = 0.0f;       // radians
        float cloudSoftness = 0.26f;        // density smoothstep width (small = crisp edges)
        float cloudDensity = 1.58f;          // extinction strength (high = opaque cores)
        float cloudSharpness = 1.0f;       // density remap contrast (high = hard-edged shapes)
        float cloudBaseVar = 0.78f;          // per-column base/top height variation (0 = flat slab)
        float cloudShading = 0.69f;          // directional sun-shading strength

        // Stars
        float starDensity = 0.63f;
        float starSize = 1.3f;              // base star core size multiplier
        float starSizeVar = 0.62f;          // 0 = uniform size, 1 = full per-star variation (skewed small)
        float starBrightness = 1.23f;
        float starColorVar = 0.85f;         // 0 = white, 1 = full cool/warm per-star tint

        // Nebula (milky-way band)
        float nebulaIntensity = 0.2f;       // band glow strength (0 = off)
        float nebulaScale = 5.7f;           // noise frequency
        float nebulaBandWidth = 0.1f;       // gaussian width of the band around its great circle
        float nebulaDust = 1.0f;            // dark dust lane strength inside the band
        glm::vec3 nebulaAxis = glm::normalize(glm::vec3(0.706f, -0.418f, 0.572f)); // band pole

        // Moon
        glm::vec3 moonDirection = glm::normalize(glm::vec3(0.728f, 0.659f, -0.190f)); // independent of the sun
        float moonSizeDeg = 6.0f;           // disc radius (degrees); real moon is ~0.26
        float moonBrightness = 0.3f;
    };
    void setSkyParams(const SkyParams& sky) { m_skyParams = sky; }

    // Volumetric fog (froxel grid; see VolumetricFogPipeline) — the TweakPanel's "Fog" categories.
    // All UBO-driven, so changes apply live.
    struct FogParams
    {
        bool  enabled = true;
        float density = 0.020f;        // global extinction at the height base (1/m)
        float heightBase = 0.0f;       // world height where the global fog is densest
        float heightFalloff = 0.40f;   // exponential density falloff above the base (1/m)
        glm::vec3 albedo = glm::vec3(1.0f, 1.0f, 1.0f);
        float albedoIntensity = 2.0f;  // > 1 is a non-physical gain (emissive-ish fog)
        float anisotropy = 0.15f;      // HG phase g (0 = isotropic, ->1 = forward scattering)
        float range = 1024.0f;         // froxel grid far distance (m)
        float noiseScale = 0.08f;      // density noise frequency (1/m)
        float noiseStrength = 0.5f;    // 0 = uniform fog, 1 = fully modulated (dusty wisps)
        float windSpeed = 1.5f;        // noise drift (m/s)
        float temporalBlend = 0.9f;    // history blend weight (jittered Z integration)
        bool  lightShadows = true;     // shadow ray per froxel per grid light (expensive)
        int   sunRays = 1;             // sun shadow rays per froxel (RT sun mode); main perf knob
        float sunSoftness = 0.02f;     // shadow ray cone half-angle (rad); softens + decorrelates the rays
        bool  spatialFilter = true;    // 3x3 tent on the scatter grid in the integrate pass
        bool  giAmbient = true;        // GI probe ambient (off = analytic sky only, cheaper)
    };
    void setFogParams(const FogParams& fog) { m_fogParams = fog; }

    // Exposure + tonemapping, applied in the composite pass (the HDR -> display mapping) — the
    // TweakPanel's "Post" category. Baked into the composite push constants, so changes re-record.
    struct PostParams
    {
        float exposureEV = 0.0f; // exposure in stops; the shader gets exp2(exposureEV)
        int   tonemapper = 3;    // 0 = off (legacy raw clip), 1 = Reinhard, 2 = ACES, 3 = AgX
    };
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

    std::vector<RendererVKLayout::MeshInfo> m_cpuMeshInfos; // CPU copy kept for BLAS builds + capacity growth re-upload
    std::vector<RendererVKLayout::MaterialInfo> m_cpuMaterialInfos;          // for capacity growth re-upload
    std::vector<RendererVKLayout::MeshInstanceOffset> m_cpuInstanceOffsets;  // for capacity growth re-upload

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