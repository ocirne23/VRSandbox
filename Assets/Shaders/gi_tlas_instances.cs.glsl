#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

// Builds the TLAS instance array on the GPU, one VkAccelerationStructureInstanceKHR per mesh instance.
// Reuses the exact world-transform composition (renderNode * instanceOffset) from instanced_indirect.cs,
// so ray-traced geometry matches what the raster path draws. No frustum culling: GI needs off-screen
// geometry too.

struct RenderNodeTransform { vec4 posScale; vec4 quat; };
struct InMeshInstance      { uint renderNodeIdx; uint instanceOffsetIdx; uint meshIdxMaterialIdx; uint pipelineIdxAlphaMode; };
struct InMeshInstanceOffset{ vec4 posScale; vec4 quat; };

// Matches VkAccelerationStructureInstanceKHR (64 bytes): a row-major 3x4 transform, packed
// custom-index/mask and sbt-offset/flags words, then the 64-bit BLAS reference.
struct TlasInstance
{
    vec4 row0;
    vec4 row1;
    vec4 row2;
    uint instanceCustomIndexAndMask;
    uint sbtOffsetAndFlags;
    uint blasLo;
    uint blasHi;
};

layout (binding = 0, std430) readonly buffer InRenderNodeTransformsBuffer { RenderNodeTransform in_renderNodeTransforms[]; };
layout (binding = 1, std430) readonly buffer InMeshInstancesBuffer        { InMeshInstance in_instances[]; };
layout (binding = 2, std430) readonly buffer InMeshInstanceOffsetsBuffer  { InMeshInstanceOffset in_instanceOffsets[]; };
layout (binding = 3, std430) readonly buffer InBlasAddressesBuffer        { uvec2 in_blasAddresses[]; }; // uint64 split lo/hi per mesh
layout (binding = 4, std430) writeonly buffer OutTlasInstancesBuffer      { TlasInstance out_instances[]; };

layout (push_constant) uniform PushConstants { uint numInstances; } pc;

vec3 quat_transform(vec3 v, vec4 q)
{
    return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}
vec4 quat_multiply(vec4 q, vec4 p)
{
    vec4 c, r;
    c.xyz = cross(q.xyz, p.xyz);
    c.w = -dot(q.xyz, p.xyz);
    r = p * q.w + c;
    r.xyz = (q * p.w + r).xyz;
    return r;
}

layout(local_size_x = 64) in;

void main()
{
    const uint id = gl_GlobalInvocationID.x;
    if (id >= pc.numInstances)
        return;

    const InMeshInstance inst = in_instances[id];
    const uint meshIdx = inst.meshIdxMaterialIdx & 0x0000FFFFu;

    const vec4 quat   = quat_multiply(in_renderNodeTransforms[inst.renderNodeIdx].quat, in_instanceOffsets[inst.instanceOffsetIdx].quat);
    const vec4 rnPS   = in_renderNodeTransforms[inst.renderNodeIdx].posScale;
    const vec4 ioPS   = in_instanceOffsets[inst.instanceOffsetIdx].posScale;
    const vec3 pos    = rnPS.xyz + quat_transform(ioPS.xyz, in_renderNodeTransforms[inst.renderNodeIdx].quat);
    const float scale = rnPS.w * ioPS.w;

    // Rotation matrix columns (object basis vectors rotated into world), scaled uniformly.
    const vec3 col0 = quat_transform(vec3(1.0, 0.0, 0.0), quat);
    const vec3 col1 = quat_transform(vec3(0.0, 1.0, 0.0), quat);
    const vec3 col2 = quat_transform(vec3(0.0, 0.0, 1.0), quat);

    TlasInstance o;
    o.row0 = vec4(scale * col0.x, scale * col1.x, scale * col2.x, pos.x);
    o.row1 = vec4(scale * col0.y, scale * col1.y, scale * col2.y, pos.y);
    o.row2 = vec4(scale * col0.z, scale * col1.z, scale * col2.z, pos.z);
    o.instanceCustomIndexAndMask = (id & 0x00FFFFFFu) | (0xFFu << 24);     // custom = instance idx, mask = 0xFF
    o.sbtOffsetAndFlags          = (0x01u << 24);                          // flags = TriangleFacingCullDisable
    const uvec2 addr = in_blasAddresses[meshIdx];
    o.blasLo = addr.x;
    o.blasHi = addr.y;

    out_instances[id] = o;
}
