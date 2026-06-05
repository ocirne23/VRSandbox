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
    constexpr uint32 MAX_RENDER_NODES = 1024 * 4;
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
    private:
        uint32 _padding;
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