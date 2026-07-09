export module RendererVK:VolumetricFogPipeline;

import Core;
import :VK;
import :Allocator;
import :Buffer;
import :CommandBuffer;
import :ComputePipeline;
import :GraphicsPipeline;
import :DescriptorSet;
import :Sampler;
import :Layout;

// Froxel-based volumetric fog (camera-frustum-aligned 3D grid, VOL_FROXEL_* texels, exponential Z slices):
//   1. scatter   : per froxel, media density (global height fog + noise + local fog volumes) and in-scattered
//                  light (sun via TLAS ray / cascade tap, GI probe ambient, light-grid lights), temporally
//                  blended against last frame's reprojected grid (ping-ponged across frames in flight).
//   2. integrate : front-to-back accumulation along Z -> in-scatter + transmittance at every slice.
//   3. apply     : fullscreen blend in the scene-color pass (before TAA), sampling the integrated volume at
//                  each pixel's G-buffer depth: out = inScatter + sceneColor * transmittance.
// The grid resolution is fixed (independent of the window size), so no swapchain-recreate handling is needed.
export class VolumetricFogPipeline final
{
public:
    VolumetricFogPipeline() = default;
    ~VolumetricFogPipeline();
    VolumetricFogPipeline(const VolumetricFogPipeline&) = delete;

    void initialize();
    // viewCount > 1 (VR): one apply descriptor set per eye so the per-eye inline draws don't clobber each other.
    void initializeApply(vk::RenderPass renderPass, uint32 viewCount = 1);
    void reloadShaders(vk::RenderPass renderPass);

    struct RecordParams
    {
        Buffer& ubo;
        Buffer& lightInfosBuffer;
        Buffer& lightGridsBuffer;
        Buffer& lightTableBuffer;
        Buffer& fogVolumesBuffer;
        Buffer& giGridDataBuffer;
        vk::ImageView shadowMapView;
        vk::Sampler   shadowMapSampler;
        vk::AccelerationStructureKHR tlas;
    };
    void record(CommandBuffer& commandBuffer, uint32 frameIdx, const RecordParams& params);

    struct ApplyParams
    {
        Buffer& ubo;
        vk::ImageView gbufferDepthView;
        vk::Sampler   gbufferSampler;
    };
    // Records the fullscreen apply draw; the caller has begun a command buffer inside the scene-color
    // render pass and set the viewport/scissor. eye selects the per-eye depth/projection (0 = desktop/left).
    void recordApply(CommandBuffer& commandBuffer, uint32 frameIdx, uint32 eye, const ApplyParams& params);

    // Terrain height map (R32F, FOG_TERRAIN_RES^2, world-space surface height in meters), CPU-baked around
    // the camera (Procedural::TerrainStreamer): the scatter pass raises the height-fog base by a fraction
    // of it (Fog/Terrain Follow), so fog pools in valleys and clears the peaks. Same ping-pong scheme as
    // the ocean shore map (OceanSimulationPipeline::uploadShoreMap): staged into the frame slot's buffer,
    // copied into the INACTIVE image inside the primary CB, flipped at the next updateUBO together with
    // the UBO center/size (binding 10 is UPDATE_AFTER_BIND, refreshed per frame — no re-record).
    void uploadTerrainMap(std::span<const float> heightTexels, const glm::vec2& centerXZ, float worldSize, uint32 frameIdx);
    void recordTerrainUpload(CommandBuffer& commandBuffer); // no-op unless an upload is pending
    void flipTerrainMapIfPending();
    void clearTerrainMap() { m_terrainWorldSize = 0.0f; } // shader stops sampling; the images just sit unused
    void updateTerrainDescriptor(uint32 frameIdx); // points this slot's scatter set at the active image
    glm::vec2 getTerrainMapCenter() const { return m_terrainCenter; }
    float getTerrainMapWorldSize() const { return m_terrainWorldSize; }

private:
    struct ImageSet
    {
        std::array<vk::Image, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> image;
        std::array<VmaAllocation, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> memory{};
        std::array<vk::ImageView, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> view;
    };

    void buildScatterLayout(ComputePipelineLayout& layout);
    void buildIntegrateLayout(ComputePipelineLayout& layout);
    void buildApplyLayout(GraphicsPipelineLayout& layout);
    void createImageSet(ImageSet& set);
    void destroyImageSet(ImageSet& set);

    ComputePipeline m_scatterPipeline;
    ComputePipeline m_integratePipeline;
    GraphicsPipeline m_applyPipeline;
    static constexpr uint32 MAX_VIEWS = 2;
    static uint32 applySlot(uint32 frameIdx, uint32 eye) { return frameIdx * MAX_VIEWS + eye; }
    uint32 m_applyViewCount = 1;

    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_scatterSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_integrateSets;
    std::array<DescriptorSet, RendererVKLayout::NUM_FRAMES_IN_FLIGHT * MAX_VIEWS> m_applySets;

    ImageSet m_scatter;    // per-froxel in-scatter/extinction; previous frame's image is the temporal history
    ImageSet m_integrated; // accumulated in-scatter + transmittance per slice
    vk::Sampler m_sampler;

    // Terrain height ping-pong pair (see uploadTerrainMap). SHADER_READ_ONLY between uploads.
    vk::Image m_terrainImage[2]{};
    VmaAllocation m_terrainMemory[2]{};
    vk::ImageView m_terrainView[2]{};
    std::array<Buffer, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_terrainStaging; // host-visible, mapped
    std::array<std::span<uint8>, RendererVKLayout::NUM_FRAMES_IN_FLIGHT> m_terrainStagingMapped;
    Sampler m_terrainSampler; // clamp-to-edge: a world-region snapshot, not a tiling patch
    int m_terrainUploadSlot = -1; // staging slot holding a not-yet-recorded upload (-1 = none)
    uint32 m_terrainActive = 0;
    bool m_terrainFlipPending = false;
    glm::vec2 m_terrainCenter = glm::vec2(0.0f); // active map center/size (updateUBO reads these)
    float m_terrainWorldSize = 0.0f;             // 0 = no map
    glm::vec2 m_terrainPendingCenter = glm::vec2(0.0f); // center/size of the staged (not yet flipped) map
    float m_terrainPendingSize = 0.0f;
};
