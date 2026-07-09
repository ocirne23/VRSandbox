#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "shared.inc.glsl"

layout (location = 0) in vec3 in_normal;
layout (location = 1) in vec2 in_uv;
layout (location = 2) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec4 out_normal; // world-space normal, xyz (w unused)

struct MaterialInfo
{
    uint flags;
    float opacity;
    uint diffuseNormalTexIdx;
    uint metalRoughnessTexIdxAlphaMode;
};
layout (binding = 2, std430) readonly buffer InMaterials { MaterialInfo in_materialInfos[]; };
layout (binding = 6) uniform sampler2D u_textures[]; // 3/4/5 = ocean maps/shore height/fog terrain (vertex stage); 6 = highest, variable count

void main()
{
    // Alpha-masked geometry must discard its clipped texels here too, otherwise the prepass writes the whole
    // quad as a solid surface and every depth/normal consumer (RTAO, TAA, fog) sees a phantom card.
    const MaterialInfo material = in_materialInfos[in_meshIdxMaterialIdx >> 16];
    const uint alphaMode = (material.metalRoughnessTexIdxAlphaMode & 0xFFFF0000u) >> 16;
    if (alphaMode == ALPHA_MODE_MASK)
    {
        const uint diffuseTexIdx = material.diffuseNormalTexIdx & 0x0000FFFFu;
        if (texture(u_textures[nonuniformEXT(diffuseTexIdx)], in_uv).a < material.opacity)
            discard;
    }
    out_normal = vec4(normalize(in_normal), 0.0);
}
