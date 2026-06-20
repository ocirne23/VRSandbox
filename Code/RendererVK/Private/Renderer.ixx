export module RendererVK:Renderer;

import Core;
import Core.glm;
import Core.Rect;
import Core.Transform;
import Core.Camera;
import Core.VrSession;

import :Layout;
import :Instance;
import :Device;
import :OpenXRSession;
import :Allocator;
import :Surface;
import :SwapChain;
import :RenderPass;
import :Framebuffers;
import :CommandBuffer;
import :Buffer;
import :MeshDataManager;
import :DescriptorSet;
import :IndirectCullComputePipeline;
import :SkinningComputePipeline;
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
import :RenderNode;

export import Core.fwd;

export enum class EValidation { ENABLED, DISABLED };
export enum class EVSync { ENABLED, DISABLED };
export enum class EVr { ENABLED, DISABLED };

export class Renderer final
{
public:

    Renderer() {}
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer(const Renderer&&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(const Renderer&&) = delete;

    bool initialize(Window& window, EValidation validation, EVSync vsync, EVr vr = EVr::DISABLED);

    bool isVrEnabled() const { return Globals::openXR.isEnabled(); }
    bool isVrStageSpace() const { return Globals::openXR.isStageSpace(); }
    IVrSession* getVrSession() { return Globals::openXR.isEnabled() ? &Globals::openXR : nullptr; }

    const Frustum& beginFrame(const Camera& camera);
    void renderNodeThreadSafe(const RenderNode& node);
    void renderNode(const RenderNode& node);
    void addLightInfo(const RendererVKLayout::LightInfo& light);
    void addFogVolume(const RendererVKLayout::FogVolumeInfo& fogVolume);
    void addPointLight(const PointLight& light);
    void addAreaLight(const AreaLight& areaLight);
    void addSpotLight(const SpotLight& spotLight);
    void setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
    void setAmbientLight(const glm::vec3& color, float intensity) { m_skyParams.ambientColor = color; m_skyParams.ambientIntensity = intensity; }
    void setSkyRadiance(const glm::vec3& color, float intensity) { m_skyParams.skyRadianceColor = color; m_skyParams.skyRadianceIntensity = intensity; }

    void setSkyParams(const SkyParams& sky) { m_skyParams = sky; }
    void setFogParams(const FogParams& fog) { m_fogParams = fog; }
    void setPostParams(const PostParams& post) { m_postParams = post; setHaveToRecordCommandBuffers(); }
    void present();

    // Skinned mesh support (skeletal animation). A palette region holds one bone matrix per skeleton bone
    // for a skinned node; setSkinningPalette() updates it each frame from an AnimationPlayer. Each skinned
    // mesh registers an instance (a per-frame skinning compute dispatch) referencing a palette region.
    uint32 allocateSkinningPalette(uint32 boneCount);
    void setSkinningPalette(uint32 paletteHandle, std::span<const glm::mat4> palette);
    uint32 addSkinnedInstance(uint32 baseVertexOffset, uint32 skinVertexOffset, uint32 outVertexOffset, uint32 vertexCount, uint32 paletteHandle);

    uint32 getNumMeshInstances() const { return m_meshInstanceCounter; }
    uint32 getNumRenderNodes() const { return (uint32)m_renderNodeTransforms.size(); }
    uint32 getNumMeshTypes() const { return m_meshInfoCounter; }
    uint32 getNumMaterials() const { return m_materialInfoCounter; }
    uint32 getCurrentFrameIndex() const { return m_swapChain.getCurrentFrameIndex(); }

    void reloadShaders();

    void toggleGiProbeDebug() { m_giProbeDebugEnabled = !m_giProbeDebugEnabled; }
    void cycleGiProbeDebugMode() { m_giProbeDebugMode ^= 1u; setHaveToRecordCommandBuffers(); } // 0 = irradiance, 1 = cellSize/LOD

    void setWindowMinimized(bool minimized);
    void recreateWindowSurface(Window& window);
    void setViewportRect(const Rect& rect) { if (Globals::openXR.isEnabled()) return; if (rect != m_viewportRect) { m_viewportRect = rect; setHaveToRecordCommandBuffers(); } } // VR renders full-extent (no editor panel sub-rect)

    Stats getStats();

private:

    CommandBuffer& getCurrentCommandBuffer() { return m_perFrameData[m_swapChain.getCurrentFrameIndex()].primaryCommandBuffer; }

    void recordCommandBuffers();
    void recordSkinning(uint32 frameIdx);
    void recordIndirectCull(uint32 frameIdx);
    void recordLightGrid(uint32 frameIdx);
    void recordShadowCull(uint32 frameIdx);
    void recordShadowDraw(uint32 frameIdx);
    void recordStaticMesh(uint32 frameIdx);
    void recordStaticMeshInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex);
    void recordGBuffer(uint32 frameIdx);
    void recordGBufferInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex);
    void recordAOInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex);
    void recordFogApplyInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex);
    void recordTaaInto(CommandBuffer& cb, uint32 frameIdx, uint32 eyeIndex);
    void recordGiProbeDebug(uint32 frameIdx);
    void recordAO(uint32 frameIdx);
    void recordVolumetricFog(uint32 frameIdx);
    void recordFogApply(uint32 frameIdx);
    void recordTaa(uint32 frameIdx);
    void recordEyeAdaptation(uint32 frameIdx);
    void recordComposite(uint32 frameIdx);
    void createEyeCompositeTargets();
    void destroyEyeCompositeTargets();
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

    void waitForGpuAndFlushStaging();
    void growRenderNodeCapacity(uint32 needed);
    void growMeshInstanceCapacity(uint32 needed);
    void growUniqueMeshCapacity(uint32 needed);
    void growMaterialCapacity(uint32 needed);
    void growInstanceOffsetCapacity(uint32 needed);
    void growLightGridBuffers(size_t neededGridBytes, uint32 neededTableEntries);
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
    SkinningComputePipeline m_skinningComputePipeline;
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

    SkyParams m_skyParams;
    FogParams m_fogParams;
    PostParams m_postParams;
    RTParams m_rtParams;
    RTAOParams m_rtaoParams;
    TAAParams m_taaParams;

    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    glm::vec3 m_giPrevCameraPos = glm::vec3(0.0f); // last frame's camera; drives GI clipmap probe freshness

    bool   m_giProbeDebugEnabled = false;
    uint32 m_giProbeDebugMode = 0;
    float  m_giProbeDebugRadius = 0.12f;

    glm::ivec2 m_windowSize;
    Rect m_viewportRect = Rect();
    bool m_windowMinimized = false;
    bool m_vsyncEnabled = true;
    uint32 m_sceneViewCount = 1; // 2 in VR: SceneColor + forward pass are multiview (one layer per eye)

    // VR: per-eye LDR composite targets
    std::array<vk::Image, 2> m_eyeColorImage{};
    std::array<VmaAllocation, 2> m_eyeColorMem{};
    std::array<vk::ImageView, 2> m_eyeColorView{};
    std::array<vk::Framebuffer, 2> m_eyeFramebuffer{};
    std::array<DescriptorSet, 2> m_vrCompositeDescriptorSet;
    vk::Image m_eyeDepthImage;
    VmaAllocation m_eyeDepthMem = nullptr;
    vk::ImageView m_eyeDepthView;

    uint32 m_maxRenderNodes = RendererVKLayout::INITIAL_RENDER_NODES;
    uint32 m_maxUniqueMeshes = RendererVKLayout::INITIAL_UNIQUE_MESHES;
    uint32 m_maxUniqueMaterials = RendererVKLayout::INITIAL_UNIQUE_MATERIALS;
    uint32 m_maxInstanceOffsets = RendererVKLayout::INITIAL_INSTANCE_OFFSETS;
    uint32 m_maxInstanceData = RendererVKLayout::INITIAL_INSTANCE_DATA;
    uint32 m_maxTextures = RendererVKLayout::INITIAL_TEXTURES;
    uint32 m_maxGiTlasInstances = RendererVKLayout::GI_INITIAL_TLAS_INSTANCES;

    size_t m_lightGridBufferSize = RendererVKLayout::INITIAL_LIGHT_GRID_BUFFER_SIZE;
    uint32 m_lightTableEntries = RendererVKLayout::INITIAL_LIGHT_TABLE_NUM_ENTRIES;

    uint32 m_numTextureDescriptors = RendererVKLayout::INITIAL_TEXTURES;
    uint32 m_meshDataGeneration = 0; // last seen MeshDataManager::getGeneration(); change -> re-record
    uint32 m_textureGeneration = 0;  // last seen TextureManager::getGeneration(); change -> rebuild texture-array pipelines

    // Skinned mesh state. Palette regions and skinning jobs are persistent (set up at spawn); palette
    // CONTENTS are refreshed each frame via setSkinningPalette and uploaded in present().
    struct SkinningPaletteRegion { uint32 offset; uint32 boneCount; };
    std::vector<SkinningPaletteRegion> m_skinningPaletteRegions;
    std::vector<glm::mat4> m_skinningPalettes;   // CPU staging (concatenated per region), uploaded each frame
    std::vector<RendererVKLayout::SkinningPushConstants> m_skinningJobs; // one per skinned mesh instance
    uint32 m_maxSkinningPaletteEntries = RendererVKLayout::INITIAL_SKINNING_PALETTE;
    void growSkinningPaletteCapacity(uint32 needed);

    std::vector<ObjectContainer*> m_objectContainers;
    std::vector<Transform>& m_renderNodeTransforms = Globals::renderNodeTransforms;
    std::vector<uint32> m_numInstancesPerMesh;
    std::vector<uint32> m_freeRenderNodeIndexes;

    uint32 m_frameCounter = 0; // monotonic; rotates the GI probe ray/taa samples set each frame
    uint32 m_meshInfoCounter = 0;
    uint32 m_materialInfoCounter = 0;
    uint32 m_instanceOffsetCounter = 0;
    uint32 m_meshInstanceCounter = 0;
    uint32 m_lightCounter = 0;
    uint32 m_fogVolumeCounter = 0;
    uint32 m_blasBuiltCount = 0;
    uint32 m_pendingMaxInstanceData = 0;

    Buffer m_meshInfosBuffer;
    Buffer m_materialInfosBuffer;
    Buffer m_instanceOffsetsBuffer;

    struct PerFrameData
    {
        SceneColor sceneColor;
        GBuffer gbuffer;
        ShadowMap shadowMap;

        // Per-eye in VR
        std::array<DescriptorSet, 2> staticMeshPipelineDescriptorSet;
        std::array<DescriptorSet, 2> gbufferDescriptorSet;
        DescriptorSet compositeDescriptorSet;
        DescriptorSet indirectCullPipelineDescriptorSet;
        DescriptorSet skinningDescriptorSet;
        DescriptorSet lightGridPipelineDescriptorSet;
        DescriptorSet shadowCullDescriptorSet;
        DescriptorSet shadowDrawDescriptorSet;

        CommandBuffer primaryCommandBuffer;
        CommandBuffer staticMeshCommandBuffer;
        CommandBuffer gbufferCommandBuffer;
        CommandBuffer aoCommandBuffer;
        CommandBuffer indirectCullCommandBuffer;
        CommandBuffer skinningCommandBuffer;
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