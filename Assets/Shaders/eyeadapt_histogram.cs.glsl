#version 450

// Eye-adaptation pass 1: build a 256-bin luminance histogram of the TAA-resolved scene colour, restricted
// to the editor viewport rect (the area outside it holds the scene clear colour, which would bias the
// average). Luminance is binned in log2 space over [minLogLum, maxLogLum]; bin 0 collects near-black pixels
// (excluded from the average). One workgroup accumulates into shared memory, then flushes to the global
// histogram with atomics. The reduce pass (eyeadapt_reduce) turns this into an adapted exposure.

layout (local_size_x = 16, local_size_y = 16) in;

layout (binding = 0) uniform sampler2D u_resolved;
layout (binding = 1, std430) buffer Histogram { uint u_bins[256]; };
layout (binding = 2, std140) uniform Params
{
    float u_minLogLum;      // lower end of the log2-luminance range
    float u_invLogLumRange; // 1 / (maxLogLum - minLogLum)
    float u_logLumRange;
    float u_dt;
    float u_tau;
    float u_key;
    float u_minExposure;
    float u_maxExposure;
};

layout (push_constant) uniform PC
{
    ivec2 u_vpMin;        // viewport origin in the resolved image
    ivec2 u_vpSize;       // viewport size (pixels sampled)
};

shared uint s_bins[256];

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

uint binIndex(float lum)
{
    if (lum < 1e-4)
        return 0u; // near-black -> dedicated bin, ignored by the average
    float t = clamp((log2(lum) - u_minLogLum) * u_invLogLumRange, 0.0, 1.0);
    return uint(t * 254.0 + 1.0); // bins 1..255
}

void main()
{
    s_bins[gl_LocalInvocationIndex] = 0u;
    barrier();

    ivec2 id = ivec2(gl_GlobalInvocationID.xy);
    if (id.x < u_vpSize.x && id.y < u_vpSize.y)
    {
        vec3 c = texelFetch(u_resolved, u_vpMin + id, 0).rgb;
        atomicAdd(s_bins[binIndex(luminance(c))], 1u);
    }
    barrier();

    atomicAdd(u_bins[gl_LocalInvocationIndex], s_bins[gl_LocalInvocationIndex]);
}
