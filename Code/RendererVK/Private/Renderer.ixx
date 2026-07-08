export module RendererVK:Renderer;

import Core;
import Core.glm;
import Core.Rect;
import Core.Sphere;
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
import :DebugLinePipeline;
import :TaaPipeline;
import :CompositePipeline;
import :EyeAdaptationPipeline;
import :GraphicsPipeline;
import :Light;
import :GpuCrashTracker;
import :Settings;
import :RenderNode;

import Core.fwd;

export enum class EValidation { ENABLED, DISABLED };
export enum class EVSync { ENABLED, DISABLED };
export enum class EVr { ENABLED, DISABLED };

// Recycles contiguous slot ranges freed by destroyed ObjectContainers (mesh infos, materials, instance
// offsets, skinning jobs, ...). Ranges stay sorted and coalesced; allocation is best-fit so small
// requests don't shred the large holes. All quantities are element counts, not bytes.
export struct IndexRangeFreeList
{
    struct Range { uint32 base; uint32 count; };

    uint32 allocate(uint32 count)
    {
        int32 best = -1;
        for (int32 i = 0; i < (int32)m_ranges.size(); ++i)
            if (m_ranges[i].count >= count && (best < 0 || m_ranges[i].count < m_ranges[best].count))
                best = i;
        if (best < 0)
            return UINT32_MAX;
        const uint32 base = m_ranges[best].base;
        m_ranges[best].base += count;
        m_ranges[best].count -= count;
        if (m_ranges[best].count == 0)
            m_ranges.erase(m_ranges.begin() + best);
        return base;
    }

    void release(uint32 base, uint32 count)
    {
        if (count == 0)
            return;
        auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), base,
            [](const Range& range, uint32 b) { return range.base < b; });
        it = m_ranges.insert(it, Range{ base, count });
        if (auto next = it + 1; next != m_ranges.end() && it->base + it->count == next->base)
        {
            it->count += next->count;
            m_ranges.erase(next);
        }
        if (it != m_ranges.begin() && (it - 1)->base + (it - 1)->count == it->base)
        {
            (it - 1)->count += it->count;
            m_ranges.erase(it);
        }
    }

private:
    std::vector<Range> m_ranges; // sorted by base, no two adjacent
};

// One mesh LOD chain: global mesh indices per level ([0] = full resolution) sharing one set of local
// bounds (used for the projected-size selection). Registered by ObjectContainer at load, referenced by
// RenderNode::m_lodInstances, applied per frame by Renderer::selectMeshLods.
export struct MeshLodGroup
{
    uint16 meshIdx[RendererVKLayout::MAX_MESH_LODS] = {};
    uint8 numLods = 0;
    glm::vec3 center = glm::vec3(0.0f); // LOD0 local bounds
    float radius = 0.0f;
    // Geometric deviation of each level from LOD0 in mesh-local units (meshopt simplify error x mesh
    // extents), 0 for level 0. Drives screen-space-error selection: a level is usable when its error
    // projects below "LOD/Max error (px)". All-zero (authored chains without error data) falls back to
    // the projected-size metric.
    float errors[RendererVKLayout::MAX_MESH_LODS] = {};
};

export class Renderer final
{
public:

    Renderer() {}
    ~Renderer();

    bool initialize(Window& window, EValidation validation, EVSync vsync, EVr vr = EVr::DISABLED);

    const Frustum& beginFrame(const Camera& camera);
    // passMask (RendererVKLayout::PASS_* bits) selects which culled passes may draw/trace the node
    // this frame: main view, sun shadows, ray tracing (GI/RTAO/RT shadows).
    void renderNodeThreadSafe(const RenderNode& node, uint32 passMask = RendererVKLayout::PASS_ALL);
    void renderNode(const RenderNode& node, uint32 passMask = RendererVKLayout::PASS_ALL);
    void addLightInfo(const RendererVKLayout::LightInfo& light);
    void addFogVolume(const RendererVKLayout::FogVolumeInfo& fogVolume);
    void addPointLight(const PointLight& light);
    void addAreaLight(const AreaLight& areaLight);
    void addSpotLight(const SpotLight& spotLight);
    // World-space debug overlay line for this frame (physics collider wireframes etc.). color is
    // packed RGBA8 with R in the low byte. Callable any time between two present() calls.
    void addDebugLine(const glm::vec3& a, const glm::vec3& b, uint32 color)
    {
        m_debugLineVerts.push_back({ a, color });
        m_debugLineVerts.push_back({ b, color });
    }
    void setSunLight(const glm::vec3& direction, const glm::vec3& color, float intensity);
    void present();

    void setAmbientLight(const glm::vec3& color, float intensity) { m_skyParams.ambientColor = color; m_skyParams.ambientIntensity = intensity; }
    void setSkyRadiance(const glm::vec3& color, float intensity) { m_skyParams.skyRadianceColor = color; m_skyParams.skyRadianceIntensity = intensity; }
    void setSkyParams(const SkyParams& sky) { m_skyParams = sky; }
    void setFogParams(const FogParams& fog) { m_fogParams = fog; }
    void setPostParams(const PostParams& post) { m_postParams = post; setHaveToRecordCommandBuffers(); }

    uint32 getNumMeshInstances() const { return m_meshInstanceCounter; }
    uint32 getNumMeshTypes() const { return m_meshInfoCounter; }
    uint32 getNumMaterials() const { return m_materialInfoCounter; }
    uint32 getCurrentFrameIndex() const { return m_swapChain.getCurrentFrameIndex(); }

    void reloadShaders();

    // Sun cascade matrices computed by beginFrame, for CPU-side shadow-caster queries.
    // Zero cascades when RT sun shadows replace the cascade maps.
    uint32 getNumSunCascades() const { return m_numSunCascades; }
    const glm::mat4* getSunCascadeViewProj() const { return m_sunCascadeViewProj; }

    // The culling view-projection beginFrame built (VR: head-centred two-eye union), for CPU-side
    // occlusion rasterization against the same view the GPU cull uses.
    const glm::mat4& getCenterViewProj() const { return m_centerViewProj; }

    bool isVrEnabled() const { return Globals::openXR.isEnabled(); }
    bool isVrStageSpace() const { return Globals::openXR.isStageSpace(); }
    IVrSession* getVrSession() { return Globals::openXR.isEnabled() ? &Globals::openXR : nullptr; }

    void toggleGiProbeDebug() { m_giProbeDebugEnabled = !m_giProbeDebugEnabled; }
    void cycleGiProbeDebugMode() { m_giProbeDebugMode ^= 1u; setHaveToRecordCommandBuffers(); } // 0 = irradiance, 1 = cellSize/LOD

    void setWindowMinimized(bool minimized);
    void recreateWindowSurface(Window& window);
    void setViewportRect(const Rect& rect) { if (Globals::openXR.isEnabled()) return; if (rect != m_viewportRect) { m_viewportRect = rect; setHaveToRecordCommandBuffers(); } } // VR renders full-extent (no editor panel sub-rect)

    Stats getStats();

    // Read by Entity's World to build scene-cache cook options (LOD generation params are baked into
    // cooked files, so they participate in the cache's options hash).
    const MeshLodParams& getLodParams() const { return m_lodParams; }

    // Mesh streaming (MeshStreamer): rewrite one MeshInfo in the CPU backing store + GPU buffer.
    // Streamed-out meshes keep their bounds but draw zero indices, so the cull's DGC draws, the shadow
    // pass, and the TLAS-instance writer all become no-ops for them without any re-record.
    void setMeshStreamedOut(uint16 meshInfoIdx);
    void setMeshStreamedIn(uint16 meshInfoIdx, int32 vertexOffset, uint32 firstIndex, uint32 indexCount);

private:

    Renderer(const Renderer&) = delete;
    Renderer(const Renderer&&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(const Renderer&&) = delete;

    CommandBuffer& getCurrentCommandBuffer() { return m_perFrameData[m_swapChain.getCurrentFrameIndex()].primaryCommandBuffer; }

    void recordCommandBuffers();
    // Rewrites swapped streamed-texture slots in this frame slot's bindless texture arrays (all
    // consuming pipelines). Called from recordCommandBuffers, where the slot's fence has been waited.
    void applyPendingTextureDescriptorWrites(uint32 frameIdx);
    // Texture-streaming priority pass: reports the node's projected screen size to the TextureStreamer
    // for every material texture its instances sample (called from renderNode/renderNodeThreadSafe).
    void noteTextureUse(const RenderNode& node, uint32 passMask);
    // Per-instance mesh LOD selection: redirects a node's LOD-chained instances (freshly copied into
    // this frame's mapped instance buffer at instances[]) to the level their projected size wants,
    // keeping the per-mesh instance counts in step. Off-screen nodes (no PASS_MAIN in passMask) take
    // two levels coarser. threadSafe = called from renderNodeThreadSafe.
    void selectMeshLods(const RenderNode& node, RendererVKLayout::InMeshInstance* instances, uint32 passMask, bool threadSafe);
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
    void recordDebugLines(uint32 frameIdx);
    void recordAO(uint32 frameIdx);
    void recordVolumetricFog(uint32 frameIdx);
    void recordFogApply(uint32 frameIdx);
    void recordTaa(uint32 frameIdx);
    void recordEyeAdaptation(uint32 frameIdx);
    void recordComposite(uint32 frameIdx);
    // Re-allocates the variable-count texture-array descriptors when the live texture count outgrew them
    // (TextureManager::getGeneration bumped). Runs at beginFrame AND again in present() before recording,
    // because containers loaded after beginFrame (terrain streaming, mid-frame spawns) upload textures
    // that this frame's record would otherwise write past the descriptor capacity.
    void syncTextureDescriptorCapacity();
    void createEyeCompositeTargets();
    void destroyEyeCompositeTargets();
    bool recordGlobalIllum(uint32 frameIdx); // returns true if the ray-tracing TLAS handle changed this frame
    void setHaveToRecordCommandBuffers();
    void recreateSwapchain();
    void createLightGridBuffers();
    void initImgui(Window& window);

    friend class ObjectContainer;
    void addObjectContainer(ObjectContainer* pObjectContainer);
    // Container teardown (~ObjectContainer): returns every renderer resource the container (and its
    // parked skinned bundles) allocated to the free lists. All RenderNodes spawned from the container
    // must have been destroyed first. CPU-side slots recycle immediately; vk objects that in-flight
    // frames may still reference (texture images, static BLASes) are retired and destroyed after the
    // GPU drain this queues (m_sharedBufferNeedSync / the AS retire queue).
    void removeObjectContainer(ObjectContainer* pObjectContainer);
    // Neutralizes MeshInfo slots (zero indexCount = draws/TLAS writes no-op, like streamed-out meshes),
    // retires their BLASes/aliases and returns the range to the free list.
    void freeMeshInfoRange(uint32 baseMeshInfoIdx, uint32 count);
    // Full teardown of a PARKED bundle (container destruction): frees the per-instance MeshInfo range,
    // output vertex regions, palette region, skinning-job slots and LOD groups its spawn allocated.
    void destroySkinnedBundle(uint32 bundleHandle);
    // Destroys textures queued by removeObjectContainer. Caller guarantees the GPU is idle; the freed
    // slots' bindless entries rewrite to the fallback when each frame slot next records.
    void processPendingTextureFrees();

    uint32 addRenderNodeTransform(const Transform& transform);
    // vertexCounts: exact per-MeshInfo vertex count (parallel to meshInfos) — the BLAS builder needs a
    // tight maxVertex per mesh and offsets are no longer monotonic with the mesh streamer's free-list.
    // skinnedOutputs: MeshInfos pointing at per-instance skinned output regions — excluded from the
    // one-time static BLAS builds (and thus compaction): their regions are uninitialized until the
    // skinning compute runs, and their addresses are owned per frame slot by the skinned BLAS rebuild.
    uint32 addMeshInfos(const std::vector<RendererVKLayout::MeshInfo>& meshInfos, std::span<const uint32> vertexCounts, bool skinnedOutputs = false);
    uint16 getRtMeshAlias(uint16 meshIdx) const { return (uint16)m_accelStructure.getMeshAlias(meshIdx); }
    uint32 addMaterialInfos(const std::vector<RendererVKLayout::MaterialInfo>& materialInfos);
    uint32 addMeshInstanceOffsets(const std::vector<RendererVKLayout::MeshInstanceOffset>& meshInstanceOffsets);
    uint32 addMeshLodGroup(const MeshLodGroup& group)
    {
        uint32 groupIdx;
        if (!m_freeMeshLodGroups.empty())
        {
            groupIdx = m_freeMeshLodGroups.back();
            m_freeMeshLodGroups.pop_back();
            m_meshLodGroups[groupIdx] = group;
        }
        else
        {
            m_meshLodGroups.push_back(group);
            groupIdx = (uint32)m_meshLodGroups.size() - 1;
        }
        // One shared BLAS per chain: every level aliases the RT level's geometry (rays don't need
        // per-level fidelity), so only that level's BLAS is ever built.
        const uint8 rtLevel = (uint8)std::clamp(m_rtParams.blasLodLevel, 0, (int)group.numLods - 1);
        for (uint8 k = 0; k < group.numLods; ++k)
            m_accelStructure.setMeshAlias(group.meshIdx[k], group.meshIdx[rtLevel]);
        return groupIdx;
    }
    uint32 addSkinnedMeshSources(const std::vector<RendererVKLayout::SkinnedMeshSource>& sources);
    const RendererVKLayout::SkinnedMeshSource& getSkinnedMeshSource(uint32 idx) const { return m_skinnedMeshSources[idx]; }

    // Everything one spawnSkinnedNode allocated (per-instance MeshInfo range, output vertex regions,
    // palette region, contiguous SkinningJob/SkinnedBlasBuild range), recycled as a unit: destroying the
    // node parks its bundle (jobs/BLAS deactivated IN PLACE — the per-frame skinned BLAS slots are
    // positional and sized once, so entries never move) on a per-container free list, and the next
    // spawnSkinnedNode of the same container reuses it wholesale with zero GPU uploads or re-records.
    struct SkinnedInstanceBundle
    {
        uint32 sourceKey;    // owning container's base SkinnedMeshSource index (free-list key)
        uint32 baseMeshIdx;  // first MeshInfo of the per-instance range (level 0s; LOD levels follow)
        uint32 paletteHandle;
        uint32 firstJob;     // first entry of the contiguous SkinningJob/SkinnedBlasBuild range
        uint32 numMeshes;
        std::vector<uint32> lodGroupForMesh; // per mesh: MeshLodGroup idx (UINT32_MAX = no chain)
        std::vector<uint32> blasIndexCounts; // per mesh: the skinned BLAS's index count (its RT level's,
                                             // restored on unpark — src.indexCount would be level 0's)
    };
    uint32 acquireSkinnedBundle(uint32 sourceKey); // reactivates + returns a parked bundle, UINT32_MAX if none free
    uint32 registerSkinnedBundle(const SkinnedInstanceBundle& bundle);
    void releaseSkinnedBundle(uint32 bundleHandle);
    const SkinnedInstanceBundle& getSkinnedBundle(uint32 handle) const { return m_skinnedBundles[handle]; }

    friend class RenderNode;
    void freeRenderNode(RenderNode& node);
    inline Transform& getRenderNodeTransform(uint32 idx) { return m_renderNodeTransforms[idx]; }

    friend class AnimatorComponent;
    uint32 allocateSkinningPalette(uint32 boneCount);
    void setSkinningPalette(uint32 paletteHandle, std::span<const glm::mat4> palette);
    // A bundle's SkinningJob/SkinnedBlasBuild entries must be one contiguous range (the per-frame
    // skinned BLAS slots are positional), so the range is allocated as a block — from the free list
    // (destroyed containers) when one fits, appended otherwise — and filled per mesh afterwards.
    uint32 allocateSkinningJobRange(uint32 count);
    void setSkinnedInstance(uint32 jobIdx, uint32 baseVertexOffset, uint32 skinVertexOffset, uint32 outVertexOffset, uint32 vertexCount, uint32 paletteHandle, uint32 meshIdx, uint32 firstIndex, uint32 indexCount);

    void waitForGpuAndFlushStaging();
    void growRenderNodeCapacity(uint32 needed);
    void growMeshInstanceCapacity(uint32 needed);
    void growUniqueMeshCapacity(uint32 needed);
    void growMaterialCapacity(uint32 needed);
    void growInstanceOffsetCapacity(uint32 needed);
    void growLightGridBuffers(size_t neededGridBytes, uint32 neededTableEntries);
    void checkLightGridCapacity();

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
    DebugLinePipeline m_debugLinePipeline;
    std::vector<DebugLinePipeline::LineVertex> m_debugLineVerts; // CPU staging, drained in present()

    SkyParams m_skyParams;
    FogParams m_fogParams;
    PostParams m_postParams;
    RTParams m_rtParams;
    RTAOParams m_rtaoParams;
    TAAParams m_taaParams;

    glm::vec3 m_cameraPos = glm::vec3(0.0f);
    glm::vec3 m_giPrevCameraPos = glm::vec3(0.0f); // last frame's camera; drives GI clipmap probe freshness
    float m_mipPixelScale = 0.0f; // viewportHeight / tan(fovY/2): projected diameter px = radius * scale / dist
    MeshLodParams m_lodParams;

    glm::mat4 m_sunCascadeViewProj[RendererVKLayout::NUM_SHADOW_CASCADES];
    uint32 m_numSunCascades = 0;
    glm::mat4 m_centerViewProj = glm::mat4(1.0f);

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

    std::vector<Transform>& m_renderNodeTransforms = Globals::renderNodeTransforms;

    // Skinned mesh state. Palette regions and skinning jobs are persistent (set up at spawn); palette
    // CONTENTS are refreshed each frame via setSkinningPalette and uploaded in present().
    struct SkinningPaletteRegion { uint32 offset; uint32 boneCount; };
    std::vector<SkinningPaletteRegion> m_skinningPaletteRegions;
    std::vector<glm::mat4> m_skinningPalettes;   // CPU staging (concatenated per region), uploaded each frame
    std::vector<RendererVKLayout::SkinningJob> m_skinningJobs; // one per skinned mesh instance; uploaded each frame
    std::vector<AccelerationStructure::SkinnedBlasBuild> m_skinnedBlasBuilds; // parallel to m_skinningJobs; per-frame BLAS rebuild
    std::vector<RendererVKLayout::SkinnedMeshSource> m_skinnedMeshSources; // per-container skinned-mesh source data (CPU-only)
    uint32 m_maxSkinningPaletteEntries = RendererVKLayout::INITIAL_SKINNING_PALETTE;
    uint32 m_maxSkinningJobs = RendererVKLayout::INITIAL_SKINNING_JOBS;
    void growSkinningPaletteCapacity(uint32 needed);
    void growSkinningJobCapacity(uint32 needed);

    std::vector<ObjectContainer*> m_objectContainers;
    std::vector<uint32> m_numInstancesPerMesh;
    std::vector<uint32> m_meshVertexCounts;    // exact per-MeshInfo vertex count (BLAS maxVertex)
    std::vector<uint8> m_meshIsSkinnedOutput;  // per MeshInfo: skinned output region (no static BLAS build)
    std::vector<uint32> m_pendingBlasRebuilds; // re-streamed meshes awaiting a BLAS rebuild in recordGlobalIllum
    std::vector<MeshLodGroup> m_meshLodGroups;
    std::array<uint32, RendererVKLayout::MAX_MESH_LODS> m_lodInstanceCounts{}; // per-level picks this frame (stats)
    std::vector<uint32> m_freeRenderNodeIndexes;
    std::vector<SkinnedInstanceBundle> m_skinnedBundles;
    std::unordered_map<uint32, std::vector<uint32>> m_freeSkinnedBundles; // sourceKey -> parked bundle handles

    // Slot recycling for destroyed ObjectContainers: the shared info/offset buffers and CPU arrays are
    // append-only (holes are never compacted), freed ranges are reused by later containers instead.
    IndexRangeFreeList m_freeMeshInfoSlots;
    IndexRangeFreeList m_freeMaterialSlots;
    IndexRangeFreeList m_freeInstanceOffsetSlots;
    IndexRangeFreeList m_freeSkinnedSourceSlots;
    IndexRangeFreeList m_freeSkinningJobSlots;      // freed slots stay inert (vertexCount/indexCount 0)
    std::vector<uint32> m_freeMeshLodGroups;
    std::vector<uint32> m_freeSkinningPaletteHandles; // regions reused on exact boneCount match
    std::vector<uint32> m_freeSkinnedBundleSlots;
    std::vector<uint16> m_pendingTextureFrees; // images possibly still sampled in flight; freed in present() after the GPU drain

    uint32 m_frameCounter = 0; // monotonic; rotates the GI probe ray/taa samples set each frame
    uint32 m_meshInfoCounter = 0;
    uint32 m_materialInfoCounter = 0;
    uint32 m_instanceOffsetCounter = 0;
    uint32 m_meshInstanceCounter = 0;
    uint32 m_lightCounter = 0;
    uint32 m_fogVolumeCounter = 0;
    uint32 m_blasBuiltCount = 0;
    uint32 m_pendingMaxInstanceData = 0;
    // Set by a within-capacity contents-only upload into m_meshInfosBuffer / m_materialInfosBuffer /
    // m_instanceOffsetsBuffer (addMeshInfos, setMeshStreamedIn/Out, addMaterialInfos, addMeshInstanceOffsets).
    // These buffers are shared, not per-frame-in-flight, and are read full-range every frame by GI/RTAO
    // compute and the draw passes; present() drains the GPU once before flushing staging if this is set, so
    // the queued copy never races an in-flight frame's read. See waitForGpuAndFlushStaging for the
    // analogous grow (over-capacity) path.
    bool m_sharedBufferNeedSync = false;

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
        CommandBuffer debugLineCommandBuffer;
        CommandBuffer taaCommandBuffer;
        CommandBuffer eyeAdaptCommandBuffer;
        CommandBuffer compositeCommandBuffer;

        bool updated = false;
        Buffer ubo;
        Buffer inRenderNodeTransformsBuffer;
        Buffer inNodePassMasksBuffer; // uint per render node: PASS_* bits, written at push time
        Buffer inMeshInstancesBuffer;
        Buffer inFirstInstancesBuffer;
        // This frame's unique-mesh count, read on the GPU by the DGC executes (sequenceCountAddress) and
        // the G-buffer's drawIndexedIndirectCount, so registering new meshes never re-records.
        Buffer meshCountBuffer;
        std::span<uint32> mappedMeshCount;

        Buffer lightInfosBuffer;
        Buffer lightGridsBuffer;
        Buffer lightTableBuffer;

        RendererVKLayout::Ubo* mappedUniformBuffer = nullptr;
        std::span<RendererVKLayout::RenderNodeTransform> mappedRenderNodeTransforms;
        std::span<uint32> mappedNodePassMasks;
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