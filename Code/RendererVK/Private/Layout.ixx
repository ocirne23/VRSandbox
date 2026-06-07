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
    constexpr uint32 MAX_INSTANCE_DATA = 1024 * 2024;
    constexpr uint32 MAX_TEXTURES = 1024;

    static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
    static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

    constexpr size_t MAX_LIGHTS = USHRT_MAX - 1;
    constexpr size_t LIGHT_GRID_BUFFER_SIZE = 10 * 1024 * 1024;
	constexpr size_t LIGHT_TABLE_NUM_ENTRIES = 1024 * 8; // should be power of 2 for hashing

    // Sun shadow cascaded shadow maps. NUM_SHADOW_CASCADES must match the count in shared.inc.glsl.
    constexpr uint32 NUM_SHADOW_CASCADES = 6;
    constexpr uint32 SHADOW_MAP_RESOLUTION = 2048;

    // Diffuse GI irradiance probes. A fixed DIM^3 camera-anchored probe volume (no hash grid). These
    // MUST match Assets/Shaders/gi_probe.inc.glsl.
    constexpr uint32 GI_PROBE_DIM = 8;                                                   // probes per axis
    constexpr float  GI_PROBE_SPACING = 4.0f;                                            // world units between probes (volume = DIM*spacing)
    constexpr uint32 GI_SH_STRIDE = 12;                                                  // SH-L1 RGB floats per probe
    constexpr uint32 GI_PROBE_COUNT = GI_PROBE_DIM * GI_PROBE_DIM * GI_PROBE_DIM;

    constexpr size_t GI_PROBE_SH_BUFFER_SIZE = (size_t)GI_PROBE_COUNT * GI_SH_STRIDE * sizeof(float);
    constexpr size_t GI_VOLUME_BUFFER_SIZE   = 4 * sizeof(int32);                        // ivec4 volumeMin (xyz) + pad

    constexpr uint32 GI_MAX_TLAS_INSTANCES = 256 * 1024;
    constexpr size_t GI_TLAS_INSTANCE_SIZE = 64;                                         // sizeof(VkAccelerationStructureInstanceKHR)

    // Trace tuning (passed via push constants). Rays are amortized over frames via the temporal blend.
    constexpr uint32 GI_RAYS_PER_PROBE = 1;
    constexpr float  GI_TEMPORAL_ALPHA = 0.04f;
    constexpr float  GI_MAX_RAY_DIST   = 64.0f;
    constexpr float  GI_SKY_INTENSITY  = 1.0f;

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
        uint32 _padding;
        // Sun + cascaded shadow map data (consumed by the fragment shader; the vertex/cull shaders
        // declare a shorter UBO block and simply ignore these trailing fields).
        glm::vec4 sunDirection;  // xyz = normalized direction towards the sun, w unused
        glm::vec4 sunColor;      // rgb = color * intensity
        // Each cascadeViewProj has a structurally-zero bottom row ([0,0,0,1] for ortho*lookAt), so the
        // per-cascade far distance is stashed in m[0][3] and the world texel size in m[1][3]. Readers
        // restore the bottom row to [0,0,0,1] before using the matrix (see the shaders' cascadeMatrix).
        glm::mat4 cascadeViewProj[NUM_SHADOW_CASCADES];
        glm::vec4 shadowParams; // x = depth bias, y = normal bias (texels), z = 1/resolution, w = pcf radius
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