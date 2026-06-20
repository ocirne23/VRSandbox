#version 450

// GPU vertex skinning. Reads a base mesh's vertices + per-vertex bone influences and a bone-matrix
// palette, writes the deformed vertices (same MeshVertex format, model space) into a per-instance output
// region of the shared vertex buffer. One dispatch per skinned instance; params come in as push constants.
//
// MeshVertex is tightly packed at 48 bytes { vec3 position; vec3 normal; vec4 tangent; vec2 texCoord; }.
// A std430 struct with vec3 members would pad to 64, so the vertex buffer is indexed as a raw float array
// (12 floats per vertex) to match the CPU/vertex-input layout exactly.

layout(local_size_x = 64) in;

layout(binding = 0, std430) buffer VertexBuffer { float v_data[]; };

struct SkinningVertex { uvec4 boneIndices; vec4 boneWeights; };
layout(binding = 1, std430) readonly buffer SkinningBuffer { SkinningVertex s_data[]; };

layout(binding = 2, std430) readonly buffer PaletteBuffer { mat4 palette[]; };

layout(push_constant) uniform PushConstants
{
    uint baseVertexOffset; // MeshVertex units
    uint skinVertexOffset; // SkinningVertex units
    uint outVertexOffset;  // MeshVertex units
    uint vertexCount;
    uint paletteOffset;    // mat4 units
} pc;

void main()
{
    const uint i = gl_GlobalInvocationID.x;
    if (i >= pc.vertexCount)
        return;

    const uint baseF = (pc.baseVertexOffset + i) * 12u;
    const uint outF  = (pc.outVertexOffset + i) * 12u;

    const vec3 pos = vec3(v_data[baseF + 0u], v_data[baseF + 1u], v_data[baseF + 2u]);
    const vec3 nrm = vec3(v_data[baseF + 3u], v_data[baseF + 4u], v_data[baseF + 5u]);
    const vec4 tan = vec4(v_data[baseF + 6u], v_data[baseF + 7u], v_data[baseF + 8u], v_data[baseF + 9u]);
    const vec2 uv  = vec2(v_data[baseF + 10u], v_data[baseF + 11u]);

    const SkinningVertex sv = s_data[pc.skinVertexOffset + i];
    mat4 skin = mat4(0.0);
    for (int b = 0; b < 4; ++b)
    {
        const float w = sv.boneWeights[b];
        if (w > 0.0)
            skin += palette[pc.paletteOffset + sv.boneIndices[b]] * w;
    }

    const vec3 skPos = (skin * vec4(pos, 1.0)).xyz;
    const mat3 skRot = mat3(skin);
    const vec3 skNrm = normalize(skRot * nrm);
    const vec3 skTan = skRot * tan.xyz;

    v_data[outF + 0u] = skPos.x; v_data[outF + 1u] = skPos.y; v_data[outF + 2u] = skPos.z;
    v_data[outF + 3u] = skNrm.x; v_data[outF + 4u] = skNrm.y; v_data[outF + 5u] = skNrm.z;
    v_data[outF + 6u] = skTan.x; v_data[outF + 7u] = skTan.y; v_data[outF + 8u] = skTan.z; v_data[outF + 9u] = tan.w;
    v_data[outF + 10u] = uv.x; v_data[outF + 11u] = uv.y;
}
