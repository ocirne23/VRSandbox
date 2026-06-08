#ifndef UBO_INC_GLSL
#define UBO_INC_GLSL

#ifndef UBO_BINDING
#define UBO_BINDING 0
#endif

layout (binding = UBO_BINDING, std140) uniform UBO
{
    mat4 u_mvp;
    vec4 u_frustumPlanes[6];
    vec3 u_viewPos;
    float u_giIntensity;   // multiplier on global illumination

    vec3 u_sunDirection;   // xyz = normalized direction towards the sun, w unused
    float u_sunAngularCos; // cos of the sun disc radius (1 = point, smaller = bigger disc)
    vec3 u_sunColor;       // rgb = color * intensity
    float u_sunGlow;       // glow falloff exponent (0 = no glow); larger = tighter

    vec3  u_skyZenith;     // color along +skyUp
    float u_skyIntensity;
    vec3  u_skyHorizon;    // horizon color (perpendicular to skyUp)
    float u_ambientIntensity; // multiplier on the ambient term (horizon + zenith, without GI)
    vec3  u_skyGround;     // color along -skyUp
    float _pad1;
    vec3  u_skyUp;         // sky "up" axis (normalized); need not be world +Y (e.g. planet surface normal)
    float _pad2;

    mat4 u_cascadeViewProj[NUM_SHADOW_CASCADES];
    vec4 u_shadowParams; // x = depth bias, y = normal bias (texels), z = 1/resolution, w = pcf radius
};

// move somewhere cleaner
vec3 skyRadiance(vec3 dir)
{
    // Configurable 3-stop gradient along the sky-up axis: ground (-up) -> horizon -> zenith (+up).
    float t = dot(dir, normalize(u_skyUp));
    vec3 sky = (t >= 0.0) ? mix(u_skyHorizon, u_skyZenith, sqrt(t))
                          : mix(u_skyHorizon, u_skyGround, min(-t * 2.0, 1.0));
    return sky * u_skyIntensity;
}



#endif