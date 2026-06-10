// Volumetric fog froxel grid helpers, shared by vol_scatter / vol_integrate / vol_apply.
// The grid is a view-frustum-aligned 3D texture: XY = viewport UV, Z = exponential view-depth slices
// from VOL_FOG_NEAR to u_fogParams0.w. Dims must match RendererVKLayout::VOL_FROXEL_*.
//
// UBO fog parameter packing (ubo.inc.glsl):
//   u_fogParams0: x = global density (extinction 1/m), y = height fog base (world up), z = height falloff (1/m), w = fog range (m)
//   u_fogParams1: rgb = fog albedo (scattering tint), w = phase anisotropy g
//   u_fogParams2: x = noise scale (1/m), y = noise strength, z = wind speed (m/s), w = temporal history weight
//   u_fogParams3: x = sun boost, y = ambient boost, z = enabled, w = light shadow rays (> 0.5)
//   u_fogParams4: x = sun shadow rays per froxel, y = spatial filter (> 0.5), z = GI ambient (> 0.5), w = sun shadow cone half-angle (rad)

#ifndef VOL_FOG_INC_GLSL
#define VOL_FOG_INC_GLSL

#define VOL_FROXEL_X 160
#define VOL_FROXEL_Y 90
#define VOL_FROXEL_Z 64
#define VOL_FOG_NEAR 1.25

float volFogFar() { return max(u_fogParams0.w, VOL_FOG_NEAR + 1.0); }

// Exponential slice distribution: more resolution near the camera, where fog detail is visible.
float volSliceToViewZ(float w)
{
    const float f = volFogFar();
    return VOL_FOG_NEAR * pow(f / VOL_FOG_NEAR, w);
}

float volViewZToSlice(float viewZ)
{
    const float f = volFogFar();
    return log(max(viewZ, VOL_FOG_NEAR) / VOL_FOG_NEAR) / log(f / VOL_FOG_NEAR);
}

// Henyey-Greenstein phase. cosTheta = dot(photon travel direction, out-scatter direction); with both the
// view ray (camera -> froxel) and the light direction (light -> froxel) negated, the negations cancel,
// so callers pass dot(viewDir, dirTowardLight).
float volPhaseHG(float cosTheta, float g)
{
    const float g2 = g * g;
    const float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4);
    return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

#endif
