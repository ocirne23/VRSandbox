export module RendererVK:Layout;

import Core;
import Core.glm;
import Core.Frustum;
import Core.Transform;

export namespace RendererVKLayout
{
    constexpr uint32 NUM_FRAMES_IN_FLIGHT = 2;

    // TODO make these dynamic
    constexpr uint32 MAX_RENDER_NODES = 1024 * 512;
    constexpr uint32 MAX_UNIQUE_MESHES = 100;
    constexpr uint32 MAX_INSTANCE_OFFSETS = 1024;
    constexpr uint32 MAX_INSTANCE_DATA = 1024 * 2024;
    constexpr uint32 MAX_UNIQUE_MATERIALS = 100;
    constexpr uint32 MAX_TEXTURES = 1024;

    static_assert(MAX_UNIQUE_MESHES < USHRT_MAX);
    static_assert(MAX_UNIQUE_MATERIALS < USHRT_MAX);

    constexpr size_t MAX_LIGHTS_PER_CELL = 7; // match shader
    constexpr size_t MAX_LIGHTS = 2048;
    constexpr size_t LIGHT_GRID_SIZE = 16; // match shader
    constexpr size_t MAX_LIGHT_GRIDS = 512;
    constexpr size_t LIGHT_TABLE_SIZE = 255;
    constexpr size_t LIGHT_TABLE_NUM_ENTRIES = 8; // match shader

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
    private:
        uint32 _padding;
    };

    struct OutMeshInstance
    {
        glm::vec3 translation;
        float scale;
        glm::vec4 quat;
        uint32 meshIdxMaterialIdx;
    };

    struct alignas(16) MeshInfo
    {
        glm::vec3 center;
        float radius = 1.0f;
        uint32 indexCount;
        uint32 firstIndex;
        int32  vertexOffset;
        uint32 firstInstance;
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
    };

    struct MeshVertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec4 tangent;
        glm::vec2 texCoord;
    };

    using MeshIndex = uint32;

    struct alignas(16) LightInfo
    {
        glm::vec3 pos;
        float radius;
        glm::vec3 color;
        float intensity;
    };

    struct alignas(16) LightCell
    {
        uint16 numLights;
        uint16 lightIds[MAX_LIGHTS_PER_CELL];
    };

    struct alignas(16) LightGrid
    {
        glm::ivec3 gridMin;
        float _padding;
        LightCell cells[LIGHT_GRID_SIZE * LIGHT_GRID_SIZE * LIGHT_GRID_SIZE];
    };

    struct alignas(16) LightGridTableEntry
    {
        uint16 entries[LIGHT_TABLE_NUM_ENTRIES];
    };

    struct alignas(16) LightTableInfo
    {
        glm::ivec3 in_gridSize;
        int in_tableSize;
        // + GridHashTableEntry table[in_tableSize] in memory
    };
}