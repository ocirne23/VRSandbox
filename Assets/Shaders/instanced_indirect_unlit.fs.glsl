#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable
#extension GL_EXT_nonuniform_qualifier : enable

// Unlit fragment variant. Shares the StaticMeshGraphicsPipeline descriptor layout but only
// samples the diffuse texture modulated by the material base color, with no lighting. Used as a
// second pipeline in the device-generated-commands Indirect Execution Set to demonstrate
// per-draw shader selection.

struct MaterialInfo
{
    vec3 baseColor;
    float roughness;
    vec3 specularColor;
    float metalness;
    vec3 emissiveColor;
    uint diffuseNormalTexIdx;
    float opacity;
    uint16_t alphaMode;
};

layout (binding = 2, std430) readonly buffer InMaterialInfos
{
    MaterialInfo in_materialInfos[];
};

layout (binding = 7) uniform sampler2D u_textures[];

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec3 out_color;

void main()
{
    const uint16_t materialIdx = uint16_t((in_meshIdxMaterialIdx & 0xFFFF0000) >> 16);
    const MaterialInfo material = in_materialInfos[materialIdx];
    const uint16_t diffuseTexIdx = uint16_t(material.diffuseNormalTexIdx & 0x0000FFFF);

    const vec4 diffuseSample = texture(u_textures[diffuseTexIdx], in_uv);
    // Alpha mask (alphaMode 1): discard fragments below the cutoff (stored in material.opacity).
    if (material.alphaMode == 1u && diffuseSample.a < material.opacity)
        discard;
    out_color = diffuseSample.xyz * material.baseColor;
}
