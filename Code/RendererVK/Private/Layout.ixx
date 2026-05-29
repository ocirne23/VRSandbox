export module RendererVK:Layout;

import Core;
import Core.glm;
import Core.Frustum;
import Core.Transform;

export namespace RendererVKLayout
{
    constexpr uint32 NUM_FRAMES_IN_FLIGHT = 2;

    // TODO make these dynamic
    constexpr uint32 MAX_RENDER_NODES = 1024 * 4;
    constexpr uint32 MAX_UNIQUE_MESHES = 1024;
    constexpr uint32 MAX_UNIQUE_MATERIALS = 1024;
    constexpr uint32 MAX_INSTANCE_OFFSETS = 1024;
    constexpr uint32 MAX_INSTANCE_DATA = 1024 * 2024;
    constexpr uint32 MAX_TEXTURES = 1024;

    static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
    static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

    constexpr size_t MAX_LIGHTS = USHRT_MAX - 1;
    constexpr size_t LIGHT_GRID_BUFFER_SIZE = 10 * 1024 * 1024;
    constexpr size_t LIGHT_TABLE_NUM_ENTRIES = 4096;

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

    // One sequence in the device-generated-commands indirect buffer. Mirrors the token layout of
    // the IndirectCommandsLayout: an EXECUTION_SET token (pipelineIndex, offset 0) followed by a
    // DRAW_INDEXED token (a VkDrawIndexedIndirectCommand, offset 4). Written by the cull compute
    // shader and consumed by vkCmdExecuteGeneratedCommandsEXT; sizeof is the indirect stride.
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
        Unlit          = 1,
        LitTransparent = 2,
    };

    struct alignas(16) MaterialInfo
    {
        glm::vec3 baseColor;
        float roughness;
        glm::vec3 specularColor;
        float metalness;
        glm::vec3 emissiveColor;
        uint16 diffuseTexIdx;
        uint16 normalTexIdx;
		float  opacity;   // opacity or alpha cutoff depending on alpha mode
        uint16 alphaMode;
    private:
        uint16 _padding[5];
    };
    static_assert(sizeof(MaterialInfo) == 64);

    struct alignas(16) LightInfo
    {
        glm::vec3 pos;
        float radius;
        glm::vec3 color;
        float intensity;
    };
}