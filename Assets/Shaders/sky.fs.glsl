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

// Chapman grazing-incidence approximation (Schüler): the closed form of the optical-depth integral
// along a ray to space through an exponential atmosphere. Replaces the old SUN_STEPS inner raymarch
// (4 samples x length/exp for every view step) with a couple of sqrts; exact at the zenith, ~airmass 38
// at the horizon, and handles the below-horizon case.
float chapman(float X, float cosChi) // X = (planet radius + height) / scale height
{
	float c = sqrt(1.5707963 * X); // sqrt(pi/2 * X)
	if (cosChi >= 0.0)
		return c / ((c - 1.0) * cosChi + 1.0);
	float sinChi = sqrt(max(1.0 - cosChi * cosChi, 1e-6));
	float X0 = X * sinChi;
	return 2.0 * sqrt(1.5707963 * X0) * exp(min(X - X0, 60.0)) - c / ((c - 1.0) * (-cosChi) + 1.0);
}
// (Rayleigh, Mie) optical depth from pos to the atmosphere edge toward the sun.
vec2 sunOpticalDepth(vec3 pos, vec3 sunDir)
{
	float r = length(pos);
	float h = max(r - R_PLANET, 0.0);
	float cosChi = dot(pos, sunDir) / r;
	float odR = exp(-h / H_RAY) * H_RAY * chapman(r / H_RAY, cosChi);
	float odM = exp(-h / H_MIE) * H_MIE * chapman(r / H_MIE, cosChi);
	return vec2(odR, odM);
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

// Integer hashes (iq-style). float fract(p * K) hashes correlate at large coordinates and draw long
// straight diagonal seams through the noise field; integer mixing has no such structure.
float hash12(vec2 p)
{
	uvec2 q = uvec2(ivec2(floor(p))) * uvec2(1597334673u, 3812015801u);
	uint n = (q.x ^ q.y) * 1597334673u;
	return float(n) * (1.0 / 4294967295.0);
}
float hash13(vec3 p)
{
	uvec3 q = uvec3(ivec3(floor(p))) * uvec3(1597334673u, 3812015801u, 2798796415u);
	uint n = (q.x ^ q.y ^ q.z) * 1597334673u;
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
// True 3D value noise: unlike stacked/offset 2D layers, consecutive march samples sit in one coherent
// 3D field, so cloud shapes genuinely overhang and interpenetrate instead of reading as flat sheets.
float vnoise3(vec3 p)
{
	vec3 i = floor(p);
	vec3 f = fract(p);
	f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float n000 = hash13(i + vec3(0, 0, 0)), n100 = hash13(i + vec3(1, 0, 0));
	float n010 = hash13(i + vec3(0, 1, 0)), n110 = hash13(i + vec3(1, 1, 0));
	float n001 = hash13(i + vec3(0, 0, 1)), n101 = hash13(i + vec3(1, 0, 1));
	float n011 = hash13(i + vec3(0, 1, 1)), n111 = hash13(i + vec3(1, 1, 1));
	float nx00 = mix(n000, n100, f.x), nx10 = mix(n010, n110, f.x);
	float nx01 = mix(n001, n101, f.x), nx11 = mix(n011, n111, f.x);
	return mix(mix(nx00, nx10, f.y), mix(nx01, nx11, f.y), f.z);
}
float fbm(vec2 p)
{
	const mat2 rot = mat2(0.8, 0.6, -0.6, 0.8) * 2.02;
	float v = 0.0;
	float a = 0.5;
	for (int i = 0; i < 4; ++i)
	{
		v += a * vnoise(p);
		p = rot * p;
		a *= 0.5;
	}
	return v * 1.0667;
}
// FBM with a conservative early-out: after each octave the maximum the remaining octaves could still
// add is known, so if even that cannot reach minNeeded the result is provably below the coverage
// threshold and the remaining (most expensive, high-frequency) octaves are skipped. In cloud gaps the
// first octave rejects most samples, which is the bulk of the march's noise cost.
float fbm3(vec3 p, int octaves, float minNeeded)
{
	float norm = 1.0 - exp2(-float(octaves)); // sum of 0.5 + 0.25 + ...
	float v = 0.0;
	float a = 0.5;
	float remaining = norm;
	for (int i = 0; i < octaves; ++i)
	{
		v += a * vnoise3(p);
		remaining -= a;
		if (v + remaining < minNeeded * norm)
			return 0.0;
		p = p * 2.13 + vec3(17.7, 9.2, 31.4); // offset octaves instead of rotating (cheap in 3D)
		a *= 0.5;
	}
	return v / norm;
}

float ign(vec2 p) { return fract(52.9829189 * fract(0.06711056 * p.x + 0.00583715 * p.y)); }

const int CLOUD_STEPS = 6;

// Cloud density at a world-offset position inside the slab (x = normalized height in [0,1]).
// bottom/top shift the vertical profile per column so the deck has uneven bases and tower tops instead
// of a flat slab; sharpEx remaps the density curve (exponent < 1 = hard, filled shapes; > 1 = wispy).
float cloudDensity(vec3 q, float x, float threshold, float softness, int octaves, float bottom, float top, float sharpEx)
{
	// Profile first: outside the (column-varying) vertical extent the density is zero regardless of the
	// noise, so skip the FBM entirely (the sun-shading sample lands out of slab often).
	float profile = smoothstep(bottom, bottom + 0.18, x) * (1.0 - smoothstep(top - 0.35, top, x));
	if (profile <= 0.0)
		return 0.0;
	float field = fbm3(q, octaves, threshold);
	if (field <= threshold)
		return 0.0;
	float d = smoothstep(threshold, threshold + softness, field) * profile;
	return pow(d, sharpEx);
}

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
	vec2 windOfs = vec2(cos(u_cloudParams0.w), sin(u_cloudParams0.w)) * (u_cloudParams0.z * u_timeSeconds);

	float softness  = u_cloudParams1.x;
	float threshold = mix(0.74, 0.30, clamp(u_cloudCoverage, 0.0, 1.0));
	float baseH     = u_cloudParams0.x;
	float thickness = max(u_cloudThickness, 1.0);
	float nScale    = u_cloudParams0.y;
	float vScale    = nScale * 2.2; // vertical noise frequency relative to horizontal

	// Noise-space position for a world point: planar coords + wind drift, height as the third axis.
	// (One shared 2D domain warp at the slab entry keeps the billowy large shapes from last round.)
	vec3 entry = dir * (baseH / cosUp);
	vec2 pEntry = vec2(dot(entry, e0), dot(entry, e1)) * nScale + windOfs;
	vec2 warp = (vec2(fbm(pEntry * 1.6 + vec2(11.3, 27.8)), fbm(pEntry * 1.6 + vec2(96.4, 4.7))) - 0.5) * 1.1;

	// Per-column height variation from a low-frequency field: raises the cloud base and grows the tops
	// where the column is "strong", so the underside undulates instead of sitting on a flat plane.
	float heightVar = clamp(u_cloudParams2.z, 0.0, 1.0);
	float colNoise = fbm(pEntry * 0.35 + vec2(7.7, 13.9));
	float colBottom = colNoise * heightVar * 0.45;
	float colTop    = 1.0 - (1.0 - colNoise) * heightVar * 0.4;
	float sharpEx   = mix(2.2, 0.55, clamp(u_cloudParams2.y, 0.0, 1.0));
	#define CLOUD_Q(pos, h) vec3(vec2(dot(pos, e0), dot(pos, e1)) * nScale + windOfs + warp, (h) * vScale)

	// Noise-space step toward the sun for the Beer-Lambert light sample, ~40% of the slab.
	float sunStepWorld = thickness * 0.4;
	vec3 sunWorldStep = sunDir * sunStepWorld;
	vec3 qSunStep = vec3(vec2(dot(sunWorldStep, e0), dot(sunWorldStep, e1)) * nScale, dot(sunWorldStep, up) * vScale);

	// Front-to-back march, jittered per pixel/frame (IGN; TAA integrates it) so the step boundaries
	// dissolve into grain instead of reading as discrete stacked layers.
	float jitter = ign(gl_FragCoord.xy + float(u_frameIndex % 64u) * 5.588238);
	float T = 1.0;
	vec3 acc = vec3(0.0);
	for (int i = 0; i < CLOUD_STEPS; ++i)
	{
		float x = (float(i) + jitter) / float(CLOUD_STEPS); // 0 = slab bottom, 1 = top
		float hi = baseH + thickness * x;
		vec3 posI = dir * (hi / cosUp);
		vec3 q = CLOUD_Q(posI, thickness * x);

		float dens = cloudDensity(q, x, threshold, softness, 4, colBottom, colTop, sharpEx);
		if (dens <= 0.004)
			continue;

		// Beer-Lambert sun attenuation: one density sample toward the sun (2 cheap octaves; the low
		// frequencies dominate shadowing). exp() gives the smooth bright-top/dark-belly falloff.
		float densSun = cloudDensity(q + qSunStep, x + sunStepWorld * dot(sunDir, up) / thickness, threshold, softness, 2, colBottom, colTop, sharpEx);
		float lit = exp(-densSun * u_cloudParams1.y);
		// Powder term: thin edges in-scatter less, which reads as the puffy "dark rim" of cumulus.
		float powder = 1.0 - exp(-dens * 4.0);

		vec3 c = skyAmbient * u_cloudParams1.w
		       + sunTint * lit * mix(0.7, 1.3, powder) * mix(0.55, 1.0, x);

		// Exponential (Beer-Lambert) step opacity: u_cloudParams2.x is the extinction strength, so dense
		// cores saturate to fully opaque instead of staying translucent ("gassy") like the old linear term.
		float a = 1.0 - exp(-dens * u_cloudParams2.x);
		acc += c * a * T;
		T *= 1.0 - a;
		if (T < 0.03)
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

void main()
{
	const vec3 dir = normalize(in_pos - u_viewPos);
	const vec3 up = normalize(u_skyUp);
	const vec3 L = normalize(u_sunDirection.xyz);

	// With the sun fully off (color/intensity/scatter boost zero) the entire scattering integral is a
	// multiply by zero: skip the raymarch and keep only the cheap ground test for the branches below.
	const bool sunLit = (u_sunColor.r + u_sunColor.g + u_sunColor.b) * u_skySunParams.x > 1e-5;
	vec3 transmittance = vec3(1.0);
	bool hitGround;
	vec3 color = vec3(0.0);
	if (sunLit)
	{
		color = atmosphere(dir, L, up, transmittance, hitGround) * u_sunColor.rgb * u_skySunParams.x;

		// Artistic grade from the Sky/Gradient tweaks, multiplied over the physical scattering (~neutral
		// at the defaults; scaled so a mid-gray gradient leaves brightness unchanged). The same colors
		// drive the GI miss-path approximation (skyColor in shared.inc.glsl), so bounce light follows.
		float tUp = dot(dir, up);
		vec3 grade = (tUp >= 0.0) ? mix(u_skyHorizon, u_skyZenith, sqrt(tUp))
		                          : mix(u_skyHorizon, u_skyGround, min(-tUp * 2.0, 1.0));
		color *= grade * 1.5;
	}
	else
		hitGround = raySphere(up * (R_PLANET + 500.0), dir, R_PLANET).x > 0.0;

	const float sunElev = dot(L, up);

	if (hitGround)
	{
		// Below the horizon: the configured ground color, sun-lit and seen through the haze.
		color += u_skyGround * clamp(sunElev * 4.0 + 0.1, 0.0, 1.0) * transmittance;
	}
	else
	{
		const float cosAngle = dot(dir, L);
		if (sunLit && u_sunAngularCos < 1.0) // 1.0 disables the disc
		{
			// Feathered rim + slight limb darkening; transmittance reddens/dims it near the horizon.
			const float feather = (1.0 - u_sunAngularCos) * max(u_skySunParams.z, 0.01);
			float disc = smoothstep(u_sunAngularCos - feather * 0.5, u_sunAngularCos + feather * 0.5, cosAngle);
			float limb = mix(0.55, 1.0, smoothstep(u_sunAngularCos, 1.0, cosAngle));
			color += u_sunColor.rgb * disc * limb * transmittance;
		}
		// Sun halo: a tight Henyey-Greenstein forward lobe (smooth peak, long graceful tail) instead of a
		// pow() spike. u_sunGlow is the strength; the transmittance keeps it warm/dim near the horizon.
		if (sunLit && u_sunGlow > 0.0)
			color += u_sunColor.rgb * phaseHG(cosAngle, 0.985) * 0.015 * u_sunGlow * transmittance;

		// Stars. Visibility comes from the local sky luminance — daylight in-scatter washes them out,
		// so they fade in automatically at dusk and in dark sky regions. (The previous sun-elevation
		// ramp kept them at zero until well after sunset, which made them effectively unreachable.)
		if (u_skySunParams.w > 0.0)
		{
			float skyLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
			float starVis = clamp(1.0 - skyLum * 25.0, 0.0, 1.0);
			if (starVis > 0.0)
			{
				vec3 sd = dir * 220.0;
				vec3 cell = floor(sd);
				float h = hash13(cell);
				float gate = step(mix(0.9995, 0.995, u_skySunParams.w), h); // density: fraction of lit cells
				if (gate > 0.0)
				{
					// Round point at a hashed position inside the cell (the old version lit whole cells
					// as square blobs); size/brightness vary per star, with a slow twinkle.
					vec3 ofs = fract(h * vec3(113.1, 412.7, 743.3)) * 0.5 + 0.25;
					float core = smoothstep(0.22, 0.0, length(fract(sd) - ofs));
					float twinkle = 0.75 + 0.25 * sin(u_timeSeconds * 3.0 + h * 113.0);
					color += vec3(core * twinkle * (0.5 + h)) * starVis * transmittance.b;
				}
			}
		}

		// Moon: a sun-lit sphere shaded into the disc around moonDir. It drifts roughly opposite the sun
		// (offset sideways so it is rarely exactly full and the terminator stays visible); the phases
		// fall out of lighting the reconstructed sphere normals with the actual sun direction. Same
		// angular size as the sun, like the real moon. Clouds blend after this, so they occlude it.
		if (u_cloudParams2.w > 0.0 && u_sunAngularCos < 1.0)
		{
			vec3 moonDir = normalize(-L + up * 0.25 + cross(L, up) * 0.2);
			float cosM = dot(dir, moonDir);
			if (cosM > u_sunAngularCos * 2.0 - 1.0) // cheap bound before building the disc basis
			{
				vec3 mT = normalize(cross(moonDir, abs(moonDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0)));
				vec3 mB = cross(moonDir, mT);
				float discSin = sqrt(max(1.0 - u_sunAngularCos * u_sunAngularCos, 1e-8));
				vec2 duv = vec2(dot(dir, mT), dot(dir, mB)) / discSin;
				float r2 = dot(duv, duv);
				if (r2 < 1.0)
				{
					// Visible-hemisphere normal at this disc point; Lambert against the sun => phase.
					vec3 n = normalize(mT * duv.x + mB * duv.y - moonDir * sqrt(1.0 - r2));
					float lambert = clamp(dot(n, L), 0.0, 1.0);
					float albedo = 0.7 + 0.6 * (fbm3(n * 7.0, 3, 0.0) - 0.5); // maria/crater mottling
					float feather = smoothstep(1.0, 0.92, r2);                 // rim anti-aliasing
					color += vec3(0.93, 0.95, 1.0) * (lambert * albedo * u_cloudParams2.w) * feather * transmittance;
				}
			}
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
