export module RendererVK:Layout;

import Core;
import Core.glm;
import Core.Frustum;
import Core.Transform;

export namespace RendererVKLayout
{
    constexpr uint32 NUM_FRAMES_IN_FLIGHT = 2;
    constexpr uint16 FALLBACK_DIFFUSE_TEX_IDX = 0;
	constexpr uint16 FALLBACK_NORMAL_TEX_IDX = 1;

    // TODO make these dynamic
    constexpr uint32 MAX_RENDER_NODES = 1024 * 64;
    constexpr uint32 MAX_UNIQUE_MESHES = 1024; // match instanced_indirect.cs.glsl
    constexpr uint32 MAX_UNIQUE_MATERIALS = 1024;
    constexpr uint32 MAX_INSTANCE_OFFSETS = 1024;
    constexpr uint32 MAX_INSTANCE_DATA = 1024 * 2028;
    constexpr uint32 MAX_TEXTURES = 1024;

    static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
    static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

    constexpr size_t MAX_LIGHTS = USHRT_MAX - 1;
    constexpr size_t LIGHT_GRID_BUFFER_SIZE = 10 * 1024 * 1024;
	constexpr size_t LIGHT_TABLE_NUM_ENTRIES = 1024 * 8; // should be power of 2 for hashing

    // Sun shadow cascaded shadow maps. NUM_SHADOW_CASCADES must match the count in ubo.inc.glsl.
    constexpr uint32 NUM_SHADOW_CASCADES = 6;
    constexpr uint32 SHADOW_MAP_RESOLUTION = 2048;

    // Diffuse GI irradiance probes. A single persistent, world-space cascaded clipmap volume: GI_NUM_CASCADES
    // nested toroidal probe grids, each GI_CASCADE_PROBE_DIM^3 probes at a fixed power-of-two spacing
    // (GI_CASCADE_BASE_SPACING << cascade), camera-centered. Probes live at absolute lattice positions
    // (lc * spacing) and are addressed toroidally (slot = lc & (DIM-1)), so irradiance carries forward in
    // place across frames with no hash table, copy, or ping-pong. SH-L1 RGB per probe.
    // These MUST match Assets/Shaders/gi_probe.inc.glsl (GI_NUM_CASCADES, GI_CASCADE_PROBE_DIM,
    // GI_CASCADE_BASE_SPACING, GI_SH_STRIDE).
    constexpr uint32 GI_SH_STRIDE = 12;                                                  // SH-L1 RGB floats per probe
    constexpr uint32 GI_PROBE_STRIDE = GI_SH_STRIDE + 1;                                  // SH + 1 mean free-space distance (visibility)
    constexpr uint32 GI_NUM_CASCADES = 4;                                                // nested clipmap levels
    constexpr uint32 GI_CASCADE_PROBE_DIM = 32;                                          // probes per axis per cascade (power of two)
    constexpr uint32 GI_CASCADE_BASE_SPACING = 2;                                        // finest cascade probe spacing, world units (power of two)
    constexpr uint32 GI_CASCADE_PROBES = GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM * GI_CASCADE_PROBE_DIM;
    constexpr uint32 GI_PROBES_TOTAL = GI_NUM_CASCADES * GI_CASCADE_PROBES;

    constexpr size_t GI_GRID_DATA_BUFFER_SIZE = (size_t)GI_PROBES_TOTAL * GI_PROBE_STRIDE * sizeof(uint32);
    constexpr uint32 GI_TRACE_THREADS = GI_PROBES_TOTAL;                                 // one invocation per probe

    constexpr uint32 GI_MAX_TLAS_INSTANCES = 256 * 1024;
    constexpr size_t GI_TLAS_INSTANCE_SIZE = 64;                                         // sizeof(VkAccelerationStructureInstanceKHR)

    struct MeshVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;
    };
    using MeshIndex = uint32;

    constexpr size_t MAX_VERTEX_DATA = 1024 * 1024 * sizeof(RendererVKLayout::MeshVertex);
    constexpr size_t MAX_INDEX_DATA = 4 * 1024 * 1024 * sizeof(RendererVKLayout::MeshIndex);

    struct alignas(16) Ubo
    {
        glm::mat4 mvp;
        Frustum frustum;
        glm::vec3 viewPos;

        float giIntensity;       // multiplier on global illumination
        glm::vec3 sunDirection;  // xyz = normalized direction towards the sun, w unused
        float sunAngularCos;     // cos of the sun disc radius (1 = point, smaller = bigger disc)
        glm::vec3 sunColor;      // rgb = color * intensity
        float sunGlow;           // glow falloff exponent (0 = no glow); larger = tighter

        glm::vec3  skyZenith;     // color along +skyUp
        float skyIntensity;
        glm::vec3  skyHorizon;    // horizon color (perpendicular to skyUp)
        float ambientIntensity;   // multiplier on the ambient term (horizon + zenith, without GI)
        glm::vec3  skyGround;     // color along -skyUp
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
        glm::vec4 cloudParams1; // x = edge softness, y = sun shading strength, z = silver lining, w = ambient amount
        glm::vec4 cloudParams2; // x = density (extinction), y = sharpness, z = base/top height variation, w = moon brightness
        glm::vec4 skySunParams; // x = atmosphere scatter boost, y = Mie anisotropy g, z = sun disc feather, w = star density

        glm::mat4 invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv (screen-space passes)
        glm::mat4 prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen (temporal reuse)
        glm::mat4 prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos (disocclusion)

        glm::vec4 screenSize;   // xy = full render-target resolution (px); zw = 1/xy (screen-space AO lookup)
        glm::vec4 viewportRect; // xy = viewport min, zw = viewport size, both normalized to [0,1] of the full
                                // render target. The scene renders through this sub-rect (editor viewport panel),
                                // so screen-space reconstruction must map full-frame UV through it.
        glm::vec4 taaJitter;    // xy = this frame's TAA sub-pixel jitter in NDC (0 when TAA disabled). Applied in
                                // clip space by the rasterization vertex shaders ONLY; mvp/invMvp/prevMvp stay
                                // unjittered so reconstruction/reprojection (TAA, RTAO) is wobble-free. zw unused.

        // Volumetric fog (packing documented in vol_fog.inc.glsl)
        glm::vec4 fogParams0; // x = global density (1/m), y = height base, z = height falloff (1/m), w = range (m)
        glm::vec4 fogParams1; // rgb = fog albedo, w = phase anisotropy g
        glm::vec4 fogParams2; // x = noise scale (1/m), y = noise strength, z = wind speed (m/s), w = temporal blend
        glm::vec4 fogParams3; // x = sun boost, y = ambient boost, z = enabled, w = light shadow rays
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
    };

    // MaterialInfo::flags bits.
    constexpr uint32 MATERIAL_FLAG_NO_RAYTRACING = 1u << 31; // instance mask 0 in the TLAS: invisible to all rays

    struct alignas(16) MaterialInfo
    {
		uint32 flags;
        float opacity;
        uint16 diffuseTexIdx;
        uint16 normalTexIdx;
        uint16 metalRoughnessTexIdx;
        uint16 alphaMode;
    };

    // Volumetric fog froxel grid (view-frustum-aligned 3D textures); dims must match vol_fog.inc.glsl.
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