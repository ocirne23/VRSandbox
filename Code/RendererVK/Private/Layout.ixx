export module RendererVK.Layout;

import Core;
import Core.glm;
import Core.Frustum;
import Core.Transform;

import RendererVK.VK;

export namespace RendererVKLayout
{
    constexpr uint32 NUM_FRAMES_IN_FLIGHT = 2;

    // HDR scene color target (linear radiance until the composite's exposure + tonemap).
    constexpr vk::Format SCENE_COLOR_FORMAT = vk::Format::eR16G16B16A16Sfloat;
    constexpr uint16 FALLBACK_DIFFUSE_TEX_IDX = 0;
	constexpr uint16 FALLBACK_NORMAL_TEX_IDX = 1;

    // Initial capacities only: the Renderer tracks the live capacities and grows the backing buffers
    // at runtime when they are exceeded (GPU idle + buffer recreate + command buffer re-record).
    constexpr uint32 INITIAL_RENDER_NODES = 8;
    constexpr uint32 INITIAL_UNIQUE_MESHES = 4;
    constexpr uint32 INITIAL_UNIQUE_MATERIALS = 8;
    constexpr uint32 INITIAL_INSTANCE_OFFSETS = 64;
    constexpr uint32 INITIAL_INSTANCE_DATA = 4;
    constexpr uint32 INITIAL_TEXTURES = 64; // TextureManager grows this, clamped to the device limit
    constexpr size_t INITIAL_LIGHT_GRID_BUFFER_SIZE = 10 * 1024 * 1024;
	constexpr size_t INITIAL_LIGHT_TABLE_NUM_ENTRIES = 64; // must stay a power of 2 for hashing (doubling preserves this)


    // Mesh/material indices are stored as uint16 in InMeshInstance, so growth clamps to this.
    constexpr uint32 MESH_MATERIAL_INDEX_LIMIT = USHRT_MAX
        - 1;
    constexpr size_t MAX_LIGHTS = USHRT_MAX - 1;
    // Sun shadow cascaded shadow maps.
    constexpr uint32 NUM_SHADOW_CASCADES = 6;
    constexpr uint32 SHADOW_MAP_RESOLUTION = 2048;

    // Diffuse GI irradiance probes. A single persistent, world-space cascaded clipmap volume: GI_NUM_CASCADES
    // nested toroidal probe grids, each GI_CASCADE_PROBE_DIM^3 probes at a fixed power-of-two spacing
    // (GI_CASCADE_BASE_SPACING << cascade), camera-centered. Probes live at absolute lattice positions
    // (lc * spacing) and are addressed toroidally (slot = lc & (DIM-1)), so irradiance carries forward in
    // place across frames with no hash table, copy, or ping-pong. SH-L1 RGB per probe.
    // The GI_* sizing constants are injected into every shader compile (Shader.cpp buildLayoutPreamble).
    constexpr uint32 GI_SH_STRIDE = 12;                                                  // SH-L1 RGB floats per probe
    constexpr uint32 GI_PROBE_STRIDE = GI_SH_STRIDE + 1;                                  // SH + 1 mean free-space distance (visibility)
    constexpr uint32 GI_NUM_CASCADES = 4;                                                // nested clipmap levels
    constexpr uint32 GI_CASCADE_PROBE_DIM = 32;                                          // probes per axis per cascade (power of two)
    constexpr uint32 GI_CASCADE_BASE_SPACING = 2;                                        // finest cascade probe spacing, world units (power of two)
    constexpr uint32 GI_CASCADE_PROBES = GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM;
    constexpr uint32 GI_PROBES_TOTAL = GI_NUM_CASCADES * GI_CASCADE_PROBES;

    constexpr size_t GI_GRID_DATA_BUFFER_SIZE = (size_t)GI_PROBES_TOTAL * GI_PROBE_STRIDE * sizeof(uint32);
    constexpr uint32 GI_TRACE_THREADS = GI_PROBES_TOTAL;                                 // one invocation per probe

    constexpr uint32 GI_INITIAL_TLAS_INSTANCES = 256; // grown when the instance count exceeds it
    constexpr size_t GI_TLAS_INSTANCE_SIZE = 64;                                         // sizeof(VkAccelerationStructureInstanceKHR)

    struct MeshVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;
    };
    using MeshIndex = uint32;

    // Initial mega-buffer sizes; MeshDataManager grows them on demand (GPU copy preserves contents).
    constexpr size_t INITIAL_VERTEX_DATA = 1024 * 1024 * sizeof(RendererVKLayout::MeshVertex);
    constexpr size_t INITIAL_INDEX_DATA = 4 * 1024 * 1024 * sizeof(RendererVKLayout::MeshIndex);

    // A camera view's matrices grouped contiguously (AoS): one view's data in a single block, so on desktop
    // only views[0] is touched. views[0] = centre/combined view (the sole desktop view; in VR sized to the
    // union of both eyes' FOV so the shared world-space passes cover everything either eye sees);
    // views[1] = left eye, views[2] = right eye (VR only). Shaders select via g_viewIndex.
    struct alignas(16) ViewData
    {
        glm::mat4 mvp;
        glm::mat4 invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv
        glm::mat4 prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen
        glm::mat4 prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos
        glm::vec4 viewPos;    // xyz = world position
    };
    constexpr uint32 VIEW_CENTER = 0;  // shared passes + desktop; the eyes are 1 (left) and 2 (right)
    constexpr uint32 NUM_UBO_VIEWS = 3;
    // Maps a per-eye index (0/1, also used for per-eye history slots) to the u_views[] matrix index a
    // per-eye pass should read: desktop (viewCount 1) collapses to VIEW_CENTER; VR maps eye 0/1 -> 1/2.
    constexpr uint32 eyeToViewIndex(uint32 eye, uint32 viewCount) { return viewCount > 1 ? eye + 1 : VIEW_CENTER; }

    struct alignas(16) Ubo
    {
        ViewData views[NUM_UBO_VIEWS];
        Frustum frustum;         // centre/combined frustum (culling, shadow cascade fit)
        glm::vec3 viewPad_;      // (the centre viewPos moved into views[]) keeps betaMie in a std140 16-byte slot
        float betaMie;           // Mie scattering coefficient at sea level (1/m), drives sky + indirect sky light
        glm::vec3 sunDirection;  // xyz = normalized direction towards the sun, w unused
        float sunAngularCos;     // cos of the sun disc radius (1 = point, smaller = bigger disc)
        glm::vec3 sunColor;      // rgb = color * intensity (sun irradiance; also sources atmosphere scattering)
        float sunGlow;           // glow falloff exponent (0 = no glow); larger = tighter

        glm::vec3 betaRayleigh;  // Rayleigh scattering coefficients at sea level (1/m), drives sky + indirect
        float rolloffKnee;       // sky highlight roll-off: luminance where compression starts (at full roll-off)
        glm::vec3 skyRadianceColor; // directional atmosphere light (moonlight/space light) radiance, along skyUp; GI-only
        float rtSkyRadiance;        // > 0.5: one sky-visibility ray per GI probe gates the sky radiance injection
        glm::vec3 ambientColor;   // flat, non-physical minimum ambient radiance (applied once at final shading)
        uint32 frameIndex;        // monotonic frame counter (RNG / temporal-rotation source so passes that read
                                  // it can be recorded once instead of baking it into a push constant)
        glm::vec3  skyUp;         // sky "up" axis (normalized); need not be world +Y (e.g. planet surface normal)
        float rtSunShadow;        // > 0.5: ray-traced sun shadows instead of PCSS cascades
        // Each cascadeViewProj has a structurally-zero bottom row ([0,0,0,1] for ortho*lookAt), so the
        // per-cascade far distance is stashed in m[0][3] and the world texel size in m[1][3]. Readers
        // restore the bottom row to [0,0,0,1] before using the matrix (see the shaders' cascadeMatrix).
        glm::mat4 cascadeViewProj[NUM_SHADOW_CASCADES];
        glm::vec3 shadowParams; // x = depth bias, y = normal bias (texels), z = 1/resolution
        float sunShadowRays;    // RT sun shadow rays per pixel (1 = single jittered ray)

        float rtLightShadows;   // > 0.5: ray-traced shadows for punctual/area/tube lights
        float timeSeconds;      // elapsed app time (cloud wind / sky animation)
        float cloudCoverage;    // 0 = clear sky, 1 = overcast
        float cloudThickness;   // cloud slab thickness (m)

        glm::vec4 cloudParams0; // x = layer height (m), y = noise scale, z = wind speed (noise units/s), w = wind angle (rad)
        glm::vec4 cloudParams1; // x = edge softness, y = sun shading strength, z = unused, w = unused
        glm::vec4 cloudParams2; // x = density (extinction), y = sharpness, z = base/top height variation, w = moon brightness
        glm::vec4 skySunParams; // x = atmosphere scatter boost (in-scatter only), y = Mie anisotropy g, z = sky highlight roll-off, w = star density

        glm::vec4 screenSize;   // xy = full render-target resolution (px); zw = 1/xy (screen-space AO lookup)
        glm::vec4 viewportRect; // xy = viewport min, zw = viewport size, both normalized to [0,1] of the full
                                // render target. The scene renders through this sub-rect (editor viewport panel),
                                // so screen-space reconstruction must map full-frame UV through it.
        glm::vec4 taaJitter;    // xy = this frame's TAA sub-pixel jitter in NDC (0 when TAA disabled). Applied in
                                // clip space by the rasterization vertex shaders ONLY; mvp/invMvp/prevMvp stay
                                // unjittered so reconstruction/reprojection (TAA, RTAO) is wobble-free. zw unused.

        // Volumetric fog (packing documented in vol_fog.inc.glsl)
        glm::vec4 fogParams0; // x = global density (1/m), y = height base, z = height falloff (1/m), w = range (m)
        glm::vec4 fogParams1; // rgb = fog albedo * intensity (> 1 = non-physical gain), w = phase anisotropy g
        glm::vec4 fogParams2; // x = noise scale (1/m), y = noise strength, z = wind speed (m/s), w = temporal blend
        glm::vec4 fogParams3; // xy = unused, z = enabled, w = light shadow rays
        glm::vec4 fogParams4; // x = sun shadow rays, y = spatial filter, z = GI ambient, w = sun shadow softness (rad)

        glm::vec4 moonParams; // xyz = normalized direction towards the moon, w = cos of the moon disc radius

        glm::vec4 starParams;   // x = star size, y = size variation, z = brightness, w = color variation
        glm::vec4 nebulaParams; // x = intensity, y = noise scale, z = band width, w = dust lane strength
        glm::vec4 nebulaAxis;   // xyz = normalized milky-way band pole (band lies on its great circle), w unused

        glm::vec4 eclipseParams; // x = visible sun fraction (solar eclipse; sunColor is pre-multiplied by it,
                                 // y = sky highlight roll-off headroom (stops the shoulder absorbs),
                                 // the sky divides it back out for the unoccluded disc/corona), zw unused

        glm::vec4 atmosParams;   // x = Rayleigh scale height (m), y = Mie scale height (m), z = Mie extinction ratio, w = ozone strength
        glm::vec4 groundParams;  // rgb = ground albedo * intensity, w unused
        glm::vec4 aoParams;      // x = RTAO enabled (0/1), yzw unused
    };

    struct alignas(16) RenderNodeTransform : Transform {};
    struct alignas(16) MeshInstanceOffset {
        Transform transform;
    };

    struct alignas(16) InMeshInstance
    {
        uint32 renderNodeIdx;
        uint32 instanceOffsetIdx;
        uint16 meshIdx;
        uint16 materialIdx;
        uint16 pipelineIndex; // can use less bits probably
        uint16 alphaMode;     // can use less bits probably
    };
	static_assert(sizeof(InMeshInstance) == 16);

    struct OutMeshInstance
    {
        glm::vec3 translation;
        float scale;
        glm::vec4 quat;
        uint32 meshIdxMaterialIdx;
        uint32 _padding1;
        uint32 _padding2;
        uint32 _padding3;
    };

    // Shadow cull output: like OutMeshInstance but its trailing uint packs the alpha-mask texture index
    // (high 16 bits, 0xFFFF = opaque/no mask) and the cascade overlap bitmask (low 16 bits). Resolving
    // the material in the cull keeps the material buffer out of the depth vertex/fragment shaders.
    // Padded to the std430 array stride (48) so the GPU-side stride matches this allocation size.
    struct alignas(16) OutShadowMeshInstance
    {
        glm::vec3 translation;
        float scale;
        glm::vec4 quat;
        uint32 alphaTexIdxCascadeMask;
        uint32 _pad0;
        uint32 _pad1;
        uint32 _pad2;
    };
    static_assert(sizeof(OutShadowMeshInstance) == 48);

    struct IndirectDrawSequence
    {
        uint32 pipelineIndex;
        uint32 indexCount;
        uint32 instanceCount;
        uint32 firstIndex;
        int32  vertexOffset;
        uint32 firstInstance;
    };
    static_assert(sizeof(IndirectDrawSequence) == 24);

    struct alignas(16) MeshInfo
    {
        glm::vec3 center;
        float radius = 1.0f;
        uint32 indexCount;
        uint32 firstIndex;
        int32  vertexOffset;
        uint32 firstInstance;
    };

    enum class EAlphaMode : uint16
    {
        Opaque = 0,
        Mask   = 1,
        Blend  = 2,
    };

    enum class EPipelineIndex : uint16
    {
        LitOpaque      = 0,
        LitTransparent = 1,
        UnlitOpaque    = 2,
        UnlitTransparent = 3,
        Sky            = 4, // analytic sky + sun disc (sky sphere interior)
        WireframeTransparent = 5, // tangent-debug color, line polygon mode, alpha-blended, no depth write (debug overlay)
        GizmoUI        = 6, // tangent-debug color, vertex shader forces NDC z=0 (nearest) so it draws on top of everything and nothing draws over it (world UI)
        GizmoWorld     = 7, // tangent-debug color, depth tested, alpha-blended, no depth write (world-space gizmo occluded by geometry)
    };

    // MaterialInfo::flags bits.
    constexpr uint32 MATERIAL_FLAG_NO_RAYTRACING = 1u << 31; // instance mask 0 in the TLAS: invisible to all rays
    constexpr uint32 MATERIAL_FLAG_SKY = 1u << 30; // sky sphere: skipped in the G-buffer prepass so its depth
                                                   // stays at the far plane (TAA reprojects it parallax-free)

    struct alignas(16) MaterialInfo
    {
		uint32 flags;
        float opacity;
        uint16 diffuseTexIdx;
        uint16 normalTexIdx;
        uint16 metalRoughnessTexIdx;
        uint16 alphaMode;
    };

    // Volumetric fog froxel grid (view-frustum-aligned 3D textures).
    constexpr uint32 VOL_FROXEL_X = 160;
    constexpr uint32 VOL_FROXEL_Y = 90;
    constexpr uint32 VOL_FROXEL_Z = 64;
    constexpr uint32 MAX_FOG_VOLUMES = 256;

    // Local participating-media box, submitted per frame like lights (Renderer::addFogVolume). Density adds
    // to the global fog inside the box, fading out over the outer edgeSoftness fraction of each half extent.
    struct alignas(16) FogVolumeInfo
    {
        glm::vec3 pos;
        float density;          // extinction added inside the box (1/m)
        glm::vec3 halfExtents;
        float edgeSoftness;     // 0..1 fraction of each half extent that fades out
        glm::vec3 albedo;       // scattering tint of this volume's media
        float emissive;         // self-lit glow (radiance per meter at full density)
    };
    static_assert(sizeof(FogVolumeInfo) == 48);

    // GPU layout of the per-frame fog volume buffer: count header + array (matches vol_scatter.cs.glsl).
    struct alignas(16) FogVolumes
    {
        uint32 count;
        uint32 _pad0, _pad1, _pad2;
        FogVolumeInfo volumes[MAX_FOG_VOLUMES];
    };
    constexpr size_t FOG_VOLUME_HEADER_SIZE = sizeof(RendererVKLayout::FogVolumes) - sizeof(RendererVKLayout::FogVolumeInfo) * RendererVKLayout::MAX_FOG_VOLUMES;

    // Unified light record for both point and rectangular area lights. width == 0 marks a point
    // light (direction/rotation unused); width > 0 marks an area light whose quad height is encoded
    // in the length of direction and which is rotated by rotation around that direction.
    struct alignas(16) LightInfo
    {
        glm::vec3 pos;
        float radius;
        glm::vec3 color; // above 1.0f for higher intensity light
        float width;
        glm::vec3 direction;
        float rotation;
    };
    static_assert(sizeof(LightInfo) == 48);
}