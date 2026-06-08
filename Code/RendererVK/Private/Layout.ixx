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

    // Sun shadow cascaded shadow maps. NUM_SHADOW_CASCADES must match the count in shared.inc.glsl.
    constexpr uint32 NUM_SHADOW_CASCADES = 6;
    constexpr uint32 SHADOW_MAP_RESOLUTION = 2048;

    // Diffuse GI irradiance probes. A separate, persistent, world-space hash grid (sibling of the light
    // grid) keyed by GI_GRID_CUBE_SIZE-cubes; each occupied cube owns a dense (cube/cellSize)^3 block of
    // SH-L1 probes. Buffers are ping-ponged prev/cur across frames so irradiance carries forward.
    // These MUST match Assets/Shaders/gi_probe.inc.glsl: GI_GRID_CUBE_SIZE==GI_GRID_SIZE,
    // GI_MIN_CELL_SIZE==(1<<GI_MIN_CELL_LOG2), and log2(GI_GRID_CUBE_SIZE)==GI_MAX_CELL_LOG2. The GI cube
    // size is independent of the light grid's hash_grid GRID_SIZE and can be sized separately.
    constexpr uint32 GI_SH_STRIDE = 12;                                                  // SH-L1 RGB floats per probe
    constexpr uint32 GI_GRID_CUBE_SIZE = 8;                                             // GI probe cube size (independent of light grid)
    constexpr uint32 GI_MIN_CELL_SIZE = 1;                                               // probe density floor (1<<GI_MIN_CELL_LOG2)
    constexpr uint32 GI_MAX_CELLS_PER_AXIS = GI_GRID_CUBE_SIZE / GI_MIN_CELL_SIZE;
    constexpr uint32 GI_MAX_CELLS_PER_GRID = GI_MAX_CELLS_PER_AXIS * GI_MAX_CELLS_PER_AXIS * GI_MAX_CELLS_PER_AXIS;
    constexpr uint32 GI_MAX_GRIDS = 512;                                                 // max live cubes
    constexpr uint32 GI_TABLE_NUM_ENTRIES = 1024;                                        // power of two, > 2 * GI_MAX_GRIDS
    constexpr int32  GI_REGION_RADIUS = 3;                                               // cubes around the camera (dim = 2r+1)

    // Per-cube grid-data footprint (worst case, at the finest cellSize): header(4) + cells * SH.
    constexpr uint32 GI_GRID_WORDS_MAX = 4 + GI_MAX_CELLS_PER_GRID * GI_SH_STRIDE;
    constexpr size_t GI_GRID_DATA_BUFFER_SIZE = (size_t)GI_MAX_GRIDS * GI_GRID_WORDS_MAX * sizeof(uint32);
    constexpr size_t GI_TABLE_BUFFER_SIZE     = (size_t)(3 + GI_TABLE_NUM_ENTRIES) * sizeof(uint32);
    constexpr size_t GI_GRID_LIST_BUFFER_SIZE = (size_t)(1 + GI_MAX_GRIDS) * sizeof(uint32);
    constexpr uint32 GI_TRACE_THREADS = GI_MAX_GRIDS * GI_MAX_CELLS_PER_GRID;            // fixed trace dispatch

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
        float _pad1;
        glm::vec3  skyUp;         // sky "up" axis (normalized); need not be world +Y (e.g. planet surface normal)
        float _pad2;
        // Each cascadeViewProj has a structurally-zero bottom row ([0,0,0,1] for ortho*lookAt), so the
        // per-cascade far distance is stashed in m[0][3] and the world texel size in m[1][3]. Readers
        // restore the bottom row to [0,0,0,1] before using the matrix (see the shaders' cascadeMatrix).
        glm::mat4 cascadeViewProj[NUM_SHADOW_CASCADES];
        glm::vec4 shadowParams; // x = depth bias, y = normal bias (texels), z = 1/resolution, w = pcf radius

        glm::mat4 invMvp;     // inverse(mvp): reconstruct world pos from depth + screen uv (screen-space passes)
        glm::mat4 prevMvp;    // previous frame's mvp: reproject world pos to last frame's screen (temporal reuse)
        glm::mat4 prevInvMvp; // previous frame's inverse(mvp): reconstruct last frame's world pos (disocclusion)

        glm::vec4 screenSize;   // xy = full render-target resolution (px); zw = 1/xy (screen-space AO lookup)
        glm::vec4 viewportRect; // xy = viewport min, zw = viewport size, both normalized to [0,1] of the full
                                // render target. The scene renders through this sub-rect (editor viewport panel),
                                // so screen-space reconstruction must map full-frame UV through it.
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
    };

    struct alignas(16) MaterialInfo
    {
		uint32 flags;
        float opacity;
        uint16 diffuseTexIdx;
        uint16 normalTexIdx;
        uint16 metalRoughnessTexIdx;
        uint16 alphaMode;
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