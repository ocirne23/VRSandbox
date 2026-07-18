#version 460

// Per-emitter applied force: one thread per compacted live emitter. The opposing teams' field
// pressure is integrated over a small fixed set of sample points spread through the emitter's own
// influence region (center + 12 icosahedron directions at 0.7 * Reff) — a single center tap would
// read zero when two large bubbles press rims together without their centers overlapping. Each
// sample is weighted by the emitter's own normalized field there, so deep-contact samples dominate
// and the result is continuous as contact begins/ends. Results are written SLOT-indexed (the source
// slot rides in teamFlags.z) straight into the host-visible readback buffer; the CPU reads them
// ~2 frames later (ocean-readback contract).

layout (local_size_x = FORCE_SIM_GROUP_SIZE) in;

#include "shared.inc.glsl" // UBO + the hash-table sentinels the grid include needs
#include "force_field.inc.glsl"

layout (binding = 5, std430) writeonly buffer OutForces { vec4 out_forces[]; }; // xyz force, w pressure

const vec3 c_icoDirs[12] = vec3[12](
    vec3( 0.0,  0.5257311,  0.8506508), vec3( 0.0, -0.5257311,  0.8506508),
    vec3( 0.0,  0.5257311, -0.8506508), vec3( 0.0, -0.5257311, -0.8506508),
    vec3( 0.5257311,  0.8506508, 0.0), vec3(-0.5257311,  0.8506508, 0.0),
    vec3( 0.5257311, -0.8506508, 0.0), vec3(-0.5257311, -0.8506508, 0.0),
    vec3( 0.8506508, 0.0,  0.5257311), vec3(-0.8506508, 0.0,  0.5257311),
    vec3( 0.8506508, 0.0, -0.5257311), vec3(-0.8506508, 0.0, -0.5257311));

void main()
{
    const uint i = gl_GlobalInvocationID.x;
    if (i >= fe_count)
        return;
    const ForceEmitterData e = fe_emitters[i];
    const uint team = e.teamFlags.x;
    const float emitterOutput = max(e.outputParams.x, 1e-4);

    vec3 force = vec3(0.0);
    float pressure = 0.0;
    { // center sample: own weight is exactly 1
        float oppPhi;
        const vec3 grad = forceOpposingGradient(e.posReach.xyz, team, oppPhi);
        force += -grad;
        pressure += oppPhi;
    }
    for (uint k = 0u; k < 12u; ++k)
    {
        const vec3 dir = c_icoDirs[k];
        const float reff = e.posReach.w * forceLobe(dot(dir, e.dirFocus.xyz), e.dirFocus.w);
        const vec3 x = e.posReach.xyz + dir * (0.7 * reff);
        const float wSelf = forceContribution(x, e) / emitterOutput;
        float oppPhi;
        const vec3 grad = forceOpposingGradient(x, team, oppPhi);
        force += wSelf * -grad;
        pressure += oppPhi;
    }
    force *= u_forceParams2.w * e.outputParams.x * (1.0 / 13.0); // forceGain * Output * mean
    pressure *= 1.0 / 13.0;
    out_forces[e.teamFlags.z] = vec4(force, pressure);
}
