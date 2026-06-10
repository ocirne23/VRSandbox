#version 460

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// Sky variant of the static-mesh pipeline (EPipelineIndex::Sky), shading the inside of the sky sphere:
//  - single-scattering Rayleigh + Mie atmosphere, raymarched per pixel (VIEW_STEPS x SUN_STEPS, ALU only)
//  - sun disc attenuated by the same atmospheric transmittance (reddens and flattens at the horizon),
//    plus a Henyey-Greenstein forward-scatter halo (u_sunGlow = strength)
//  - one FBM cloud layer: domain-warped quintic value noise, wind-animated, coverage-driven, with a
//    smooth (unthresholded) directional sun-shading term and a silver lining
//  - hash stars that fade in when the sun sets
// Everything is driven from the UBO sky params (Tweaks panel: Sky / Sky/Atmosphere / Sky/Sun /
// Sky/Clouds). The GI/AO miss paths use the much cheaper analytic skyColor() (shared.inc.glsl).

#include "shared.inc.glsl"

layout (location = 0) in vec3 in_pos;
layout (location = 1) in mat3 in_tbn;
layout (location = 4) in vec2 in_uv;
layout (location = 5) in flat uint in_meshIdxMaterialIdx;

layout (location = 0) out vec4 out_color;

// ---------------------------------------------------------------------------------------------
// Atmosphere (single scattering). Earth-ish constants; heights in meters.
// ---------------------------------------------------------------------------------------------
const float R_PLANET  = 6371e3;
const float R_ATMOS   = 6451e3;
const vec3  BETA_RAY  = vec3(5.802e-6, 13.558e-6, 33.1e-6); // Rayleigh scattering at sea level
const float BETA_MIE  = 3.996e-6;                           // Mie scattering at sea level
const float MIE_EXT   = 1.11;                               // Mie extinction/scattering ratio (absorption)
const float H_RAY     = 8500.0;                             // Rayleigh scale height
const float H_MIE     = 1200.0;                             // Mie scale height
const int   VIEW_STEPS = 12;
const int   SUN_STEPS  = 4;

// Near/far intersection distances of a ray with a sphere of radius r centered at the origin.
vec2 raySphere(vec3 ro, vec3 rd, float r)
{
	float b = dot(ro, rd);
	float c = dot(ro, ro) - r * r;
	float d = b * b - c;
	if (d < 0.0) return vec2(1e20, -1e20);
	d = sqrt(d);
	return vec2(-b - d, -b + d);
}

float phaseRayleigh(float mu) { return 3.0 / (16.0 * PI) * (1.0 + mu * mu); }
// Henyey-Greenstein, used both for the Mie scattering lobe and the sun halo.
float phaseHG(float mu, float g)
{
	float g2 = g * g;
	return (1.0 - g2) / (4.0 * PI * pow(1.0 + g2 - 2.0 * g * mu, 1.5));
}

// (Rayleigh, Mie) optical depth from pos to the atmosphere edge toward the sun.
vec2 sunOpticalDepth(vec3 pos, vec3 sunDir)
{
	float tFar = raySphere(pos, sunDir, R_ATMOS).y;
	float dt = tFar / float(SUN_STEPS);
	vec2 od = vec2(0.0);
	float t = 0.5 * dt;
	for (int i = 0; i < SUN_STEPS; ++i)
	{
		float h = length(pos + sunDir * t) - R_PLANET;
		od += exp(-max(h, 0.0) / vec2(H_RAY, H_MIE)) * dt;
		t += dt;
	}
	return od;
}

// In-scattered radiance along dir (unit sun radiance; multiply by the sun color outside), plus the view
// ray's total transmittance (used to attenuate the sun disc and stars behind the atmosphere).
vec3 atmosphere(vec3 dir, vec3 sunDir, vec3 up, out vec3 transmittance, out bool hitGround)
{
	vec3 ro = up * (R_PLANET + 500.0); // observer 500m above the surface
	float tFar = max(raySphere(ro, dir, R_ATMOS).y, 0.0);
	vec2 ground = raySphere(ro, dir, R_PLANET);
	hitGround = ground.x > 0.0;
	if (hitGround)
		tFar = min(tFar, ground.x);

	float mu = dot(dir, sunDir);
	float pR = phaseRayleigh(mu);
	float pM = phaseHG(mu, u_skySunParams.y);

	vec2 odView = vec2(0.0);
	vec3 sumR = vec3(0.0), sumM = vec3(0.0);
	float dt = tFar / float(VIEW_STEPS);
	float t = 0.5 * dt;
	for (int i = 0; i < VIEW_STEPS; ++i)
	{
		vec3 p = ro + dir * t;
		float h = length(p) - R_PLANET;
		vec2 dens = exp(-max(h, 0.0) / vec2(H_RAY, H_MIE)) * dt;
		odView += dens;
		vec2 odSun = sunOpticalDepth(p, sunDir);
		vec3 tau = BETA_RAY * (odView.x + odSun.x) + vec3(BETA_MIE * MIE_EXT) * (odView.y + odSun.y);
		vec3 atten = exp(-tau);
		sumR += atten * dens.x;
		sumM += atten * dens.y;
		t += dt;
	}
	transmittance = exp(-(BETA_RAY * odView.x + vec3(BETA_MIE * MIE_EXT) * odView.y));
	return sumR * BETA_RAY * pR + sumM * vec3(BETA_MIE) * pM;
}

// ---------------------------------------------------------------------------------------------
// Clouds: a marched slab [height, height + thickness] of domain-warped FBM with a vertical density
// profile — a cheap "2.5D" volumetric: layers parallax against each other, bottoms sit lower than
// tops, edges dissolve in depth instead of looking like a painted plane.
// ---------------------------------------------------------------------------------------------

// Integer hash (iq-style). The previous fract(p * K) float hash correlates at large coordinates and
// draws long straight diagonal seams through the noise field; integer mixing has no such structure.
float hash12(vec2 p)
{
	uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334673u, 3812015801u);
	uint n = (q.x ^ q.y) * 1597334673u;
	return float(n) * (1.0 / 4294967295.0);
}
float vnoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0); // quintic fade: C2-continuous, no cell creases
	float a = hash12(i);
	float b = hash12(i + vec2(1.0, 0.0));
	float c = hash12(i + vec2(0.0, 1.0));
	float d = hash12(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p)
{
	const mat2 rot = mat2(0.8, 0.6, -0.6, 0.8) * 2.02; // rotate octaves to hide axis alignment
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 4; ++i)
	{
		v += a * vnoise(p);
		p = rot * p;
		a *= 0.5;
	}
	return v * 1.0667; // renormalize 4 octaves to ~[0,1]
}

const int CLOUD_STEPS = 5;

// rgb = cloud color, a = opacity.
vec4 clouds(vec3 dir, vec3 up, vec3 sunDir, vec3 sunTint, vec3 skyAmbient)
{
	float cosUp = dot(dir, up);
	if (cosUp <= 0.02 || u_cloudCoverage <= 0.0)
		return vec4(0.0);

	// Stable tangent basis around the sky-up axis; the slab is camera-anchored (travels with the viewer).
	vec3 refAxis = abs(up.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 e0 = normalize(cross(up, refAxis));
	vec3 e1 = cross(up, e0);
	vec2 wind = vec2(cos(u_cloudParams0.w), sin(u_cloudParams0.w)) * u_cloudParams0.z;
	vec2 windOfs = wind * u_timeSeconds;
	vec2 sunPlanar = vec2(dot(sunDir, e0), dot(sunDir, e1));

	float softness  = u_cloudParams1.x;
	float threshold = mix(0.80, 0.34, clamp(u_cloudCoverage, 0.0, 1.0));
	float baseH     = u_cloudParams0.x;
	float thickness = max(u_cloudThickness, 1.0);

	// Domain warp, computed once at the slab entry and shared by all steps: breaks the value-noise grid
	// into billows. (Per-step warp would be prettier but triples the noise cost.)
	vec3 entry = dir * (baseH / cosUp);
	vec2 pEntry = vec2(dot(entry, e0), dot(entry, e1)) * u_cloudParams0.y + windOfs;
	vec2 warp = (vec2(fbm(pEntry * 1.6 + vec2(11.3, 27.8)), fbm(pEntry * 1.6 + vec2(96.4, 4.7))) - 0.5) * 1.1;

	// Front-to-back march through the slab.
	float T = 1.0;      // transmittance
	vec3 acc = vec3(0.0);
	for (int i = 0; i < CLOUD_STEPS; ++i)
	{
		float x = (float(i) + 0.5) / float(CLOUD_STEPS); // 0 = slab bottom, 1 = top
		float hi = baseH + thickness * x;
		vec3 posI = dir * (hi / cosUp);
		// The small height-proportional offset decorrelates the layers (poor man's 3D noise), which is
		// what creates the parallax/depth impression as the view direction changes.
		vec2 pi = vec2(dot(posI, e0), dot(posI, e1)) * u_cloudParams0.y + windOfs + warp
		        + vec2(0.13, 0.31) * (hi * u_cloudParams0.y * 6.0);
		float field = fbm(pi);

		// Vertical profile: rounded bottom, domed top.
		float profile = smoothstep(0.0, 0.3, x) * smoothstep(1.0, 0.5, x);
		float dens = smoothstep(threshold, threshold + softness, field) * profile;
		if (dens <= 0.003)
			continue;

		// Directional sun shading from the RAW field difference toward the sun (thresholded differences
		// produce hard contour lines), plus a height gradient: bottoms in their own shadow, tops lit.
		float fieldSun = fbm(pi + sunPlanar * 0.15);
		float lit = clamp(0.5 - (fieldSun - field) * u_cloudParams1.y, 0.0, 1.0);
		float hShade = mix(0.5, 1.0, x);

		vec3 c = skyAmbient * u_cloudParams1.w
		       + sunTint * (0.35 + 0.85 * lit) * hShade;

		float a = dens * 0.55; // per-step opacity
		acc += c * a * T;
		T *= 1.0 - a;
		if (T < 0.04)
			break;
	}

	float alpha = 1.0 - T;
	if (alpha <= 0.001)
		return vec4(0.0);

	// Silver lining: forward-scatter highlight through the thin parts, on the accumulated result.
	float mu = max(dot(dir, sunDir), 0.0);
	vec3 col = acc / alpha + sunTint * pow(mu, 12.0) * (1.0 - alpha) * u_cloudParams1.z;

	// Horizon fade doubles as anti-aliasing: distant FBM has no mips, so melt it into the haze.
	return vec4(col, alpha * smoothstep(0.02, 0.18, cosUp));
}

// ---------------------------------------------------------------------------------------------

float hash13(vec3 p)
{
	p = fract(p * 0.1031);
	p += dot(p, p.zyx + 31.32);
	return fract((p.x + p.y) * p.z);
}

void main()
{
	const vec3 dir = normalize(in_pos - u_viewPos);
	const vec3 up = normalize(u_skyUp);
	const vec3 L = normalize(u_sunDirection.xyz);

	vec3 transmittance;
	bool hitGround;
	vec3 color = atmosphere(dir, L, up, transmittance, hitGround) * u_sunColor.rgb * u_skySunParams.x;

	const float sunElev = dot(L, up);
	const float night = clamp(-sunElev * 6.0, 0.0, 1.0);

	if (hitGround)
	{
		// Below the horizon: the configured ground color, sun-lit and seen through the haze.
		color += u_skyGround * clamp(sunElev * 4.0 + 0.1, 0.0, 1.0) * transmittance;
	}
	else
	{
		const float cosAngle = dot(dir, L);
		if (u_sunAngularCos < 1.0) // 1.0 disables the disc
		{
			// Feathered rim + slight limb darkening; transmittance reddens/dims it near the horizon.
			const float feather = (1.0 - u_sunAngularCos) * max(u_skySunParams.z, 0.01);
			float disc = smoothstep(u_sunAngularCos - feather * 0.5, u_sunAngularCos + feather * 0.5, cosAngle);
			float limb = mix(0.55, 1.0, smoothstep(u_sunAngularCos, 1.0, cosAngle));
			color += u_sunColor.rgb * disc * limb * transmittance;
		}
		// Sun halo: a tight Henyey-Greenstein forward lobe (smooth peak, long graceful tail) instead of a
		// pow() spike. u_sunGlow is the strength; the transmittance keeps it warm/dim near the horizon.
		if (u_sunGlow > 0.0)
			color += u_sunColor.rgb * phaseHG(cosAngle, 0.985) * 0.015 * u_sunGlow * transmittance;

		// Stars: hash a direction-space lattice; visible only at night and through the (dark) atmosphere.
		if (night > 0.0 && u_skySunParams.w > 0.0)
		{
			vec3 cell = floor(dir * 220.0);
			float h = hash13(cell);
			float star = smoothstep(mix(0.9995, 0.996, u_skySunParams.w), 1.0, h);
			float twinkle = 0.7 + 0.3 * sin(u_timeSeconds * 3.0 + h * 113.0);
			color += vec3(star * twinkle) * night * transmittance.b;
		}
	}

	// Clouds last, over everything (they also dim the sun disc behind them). Sun tint approximates the
	// transmittance toward the sun at cloud height: white at day, warm at the horizon, dark at night.
	const float sunsetWarm = clamp(sunElev * 5.0, 0.0, 1.0);
	const vec3 sunTint = u_sunColor.rgb * 0.30 * mix(vec3(1.0, 0.45, 0.2), vec3(1.0), sunsetWarm)
	                   * clamp(sunElev * 8.0 + 0.25, 0.02, 1.0);
	const vec3 skyAmbient = color; // local sky as the cloud's ambient source keeps hue coherent
	vec4 cl = clouds(dir, up, L, sunTint, skyAmbient);
	color = mix(color, cl.rgb, cl.a);

	// u_skyIntensity is the user brightness slider; defaults to 0.5, so scale by 2 to be neutral there.
	out_color = vec4(color * (u_skyIntensity * 2.0), 1.0);
}
