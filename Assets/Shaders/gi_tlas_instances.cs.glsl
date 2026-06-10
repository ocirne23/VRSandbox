#version 450

#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_shader_16bit_storage : enable

#include "shared.inc.glsl"

// Builds the TLAS instance array on the GPU, one VkAccelerationStructureInstanceKHR per mesh instance.
// Reuses the exact world-transform composition (renderNode * instanceOffset) from instanced_indirect.cs,
// so ray-traced geometry matches what the raster path draws. No frustum culling: GI needs off-screen
// geometry too.

struct RenderNodeTransform { vec4 posScale; vec4 quat; };
struct InMeshInstance      { uint renderNodeIdx; uint instanceOffsetIdx; uint meshIdxMaterialIdx; uint pipelineIdxAlphaMode; };
struct InMeshInstanceOffset{ vec4 posScale; vec4 quat; };
struct MaterialInfo
{
    uint flags;
    float opacity;
    uint diffuseNormalTexIdx;
    uint metalRoughnessTexIdxAlphaMode;
};

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
layout (binding = 5, std430) readonly buffer InMaterialInfosBuffer        { MaterialInfo in_materialInfos[]; };

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
    // Flags: TriangleFacingCullDisable (0x01) always; ForceOpaque (0x04) for everything except
    // alpha-masked instances, which stay non-opaque so shadow rays can run their alpha test
    // (rt_shadow.inc.glsl). Opaque-flagged rays (RTAO, GI gather) still treat masked geometry as solid.
    const uint alphaMode = inst.pipelineIdxAlphaMode >> 16;
    const uint instFlags = 0x01u | (alphaMode == ALPHA_MODE_MASK ? 0x00u : 0x04u);
    o.sbtOffsetAndFlags          = (instFlags << 24);

    // Guard the address lookup: meshIdx = (meshIdxMaterialIdx & 0xFFFF) can be up to 65535, but the BLAS
    // address buffer only has length() entries. An out-of-bounds read returns a foreign 64-bit value that,
    // used as a BLAS reference, makes ray traversal chase a wild pointer -> MMU fault. Clamp to a null
    // reference instead.
    const bool meshInRange = meshIdx < uint(in_blasAddresses.length());
    const uvec2 addr = meshInRange ? in_blasAddresses[meshIdx] : uvec2(0u);
    o.blasLo = addr.x;
    o.blasHi = addr.y;

    // Mask gates traversal: the ray uses cullMask 0xFF, so mask 0 makes this instance unhittable. Mask off
    // any instance that cannot be safely traversed:
    //  - no real BLAS (zeroed/out-of-range address) -> would chase a null/garbage pointer, and
    //  - a non-finite transform (NaN/Inf from an out-of-range renderNode/instanceOffset index) -> makes the
    //    driver's TLAS bounds garbage, which also MMU-faults traversal on NVIDIA.
    const bool hasBlas    = (addr.x != 0u || addr.y != 0u);
    const bool finiteXform = !any(isnan(o.row0)) && !any(isinf(o.row0))
                          && !any(isnan(o.row1)) && !any(isinf(o.row1))
                          && !any(isnan(o.row2)) && !any(isinf(o.row2));
    // Mask 0 also excludes debug/gizmo geometry flagged NO_RAYTRACING on its material (rasterized only,
    // never blocks shadow rays / GI / AO).
    const uint materialIdx = inst.meshIdxMaterialIdx >> 16;
    const bool noRT = materialIdx < uint(in_materialInfos.length())
                   && (in_materialInfos[materialIdx].flags & MATERIAL_FLAG_NO_RAYTRACING) != 0u;
    const uint mask = (hasBlas && finiteXform && !noRT) ? 0xFFu : 0x00u;
    o.instanceCustomIndexAndMask = (id & 0x00FFFFFFu) | (mask << 24);       // custom = instance idx

    out_instances[id] = o;
}
