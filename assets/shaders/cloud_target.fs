#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uDepthTex;
uniform sampler2D uSkyCaptureTex;
uniform sampler2D uNoiseTex;
uniform sampler3D uAtmosphereLut;
uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunLightColor;
uniform vec3 uMoonLightColor;
uniform vec3 uSkyAmbientColor;
uniform vec3 uShadowTintColor;
uniform vec3 uHorizonScatterColor;
uniform float uSkyIntensity;
uniform float uMoonVisibility;
// uCloudWetness removed — cloud_target uses uCloudWetness as single wetness contract
uniform float uSurfaceWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uPrecipitation;
uniform float uLightningFlash;
uniform float uAerialStrength;
uniform float uHorizonScatterStrength;
uniform float uSunWarmth;
uniform float uSkyCoolness;
uniform float uAerialReduction;
uniform int uCloudShadowsEnabled;
uniform float uCloudShadowStrength;
uniform float uCloudShadowScale;
uniform float uCloudShadowSpeed;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudHeight;
uniform float uCloudThickness;
uniform float uTime;
uniform float uCloudTimeScale;
uniform bool uNoiseEnabled;

// Planar cloud uniforms
uniform float uPlanarCloudCoverage;
uniform float uPlanarCloudDensity;
uniform float uPlanarCloudAltitude;

// CPU illuminance uniforms — legacy/fallback only. Cloud lighting reads from
// GPU SkyCapture metadata via getLightingEnvironment(). Kept for forward path.
uniform vec3 uDirectIlluminance;
uniform vec3 uSkyIlluminance;
uniform vec3 uSunIlluminance;
uniform vec3 uMoonIlluminance;

#include "lighting_environment.glsl"
#include "atmosphere_lut.glsl"
#include "cloud_density.glsl"

const float PHI = 1.61803398875;
const float GOLDEN_ANGLE = 6.28318530718 / (PHI + 1.0);
// DerivativeMain planet radius for sphere-intersection ray setup
const float planetRadius = 6371000.0;

vec3 saturate3(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

// DerivativeMain Deferred1.glsl:138-142 — 4-lobe phase ladder.
// Each lobe = forward HG + backward HG + Cornette-Shanks peak,
// with per-lobe G scaling (narrowing) and independent weights.
// Returns pre-weighted vec4; use directly in OD energy sums.

vec4 multiLobePhase(float cosTheta, float wetness) {
    float fG = 0.6 - wetness * 0.2;   // cloudForwardG
    float bG = -0.4 + wetness * 0.2;  // cloudBackwardG
    float bW = 0.25;                   // cloudBackwardWeight
    float pW = 0.1 + 0.7 * wetness;   // cloudPeakWeight

    float f0 = atmHenyeyGreensteinPhase(cosTheta, fG);
    float b0 = atmHenyeyGreensteinPhase(cosTheta, bG);

    vec4 phases;
    // Lobe 0 (primary): full G
    phases.x = f0 * 0.7                               + b0 * bW          + cloudCornetteShanksPhase(cosTheta, 0.9) * pW;
    // Lobe 1: G * 0.7
    phases.y = atmHenyeyGreensteinPhase(cosTheta, fG * 0.7) * 0.35       + atmHenyeyGreensteinPhase(cosTheta, bG * 0.7) * bW * 0.6  + cloudCornetteShanksPhase(cosTheta, 0.6) * pW * 0.5;
    // Lobe 2: G * 0.5
    phases.z = atmHenyeyGreensteinPhase(cosTheta, fG * 0.5) * 0.17       + atmHenyeyGreensteinPhase(cosTheta, bG * 0.5) * bW * 0.3  + cloudCornetteShanksPhase(cosTheta, 0.4) * pW * 0.2;
    // Lobe 3: G * 0.3
    phases.w = atmHenyeyGreensteinPhase(cosTheta, fG * 0.3) * 0.08       + atmHenyeyGreensteinPhase(cosTheta, bG * 0.3) * bW * 0.2  + cloudCornetteShanksPhase(cosTheta, 0.2) * pW * 0.1;

    return phases;
}

vec3 sampleAtmosphere(vec3 ray, vec3 sunDir, vec3 moonDir, float eyeAlt, float dayFactor, float moonVis) {
    vec3 transmittance;
    vec3 sunSky = atmGetSkyRadianceForLight(eyeAlt, ray, sunDir, transmittance);
    vec3 moonSky = atmGetSkyRadianceForLight(eyeAlt, ray, moonDir, transmittance) *
                   moonVis * (1.0 - dayFactor) * 0.28;
    return max(sunSky + moonSky, vec3(0.0));
}

// Sphere-intersection ray setup (DerivativeMain RaySphereIntersection)
vec2 raySphereIntersection(vec3 pos, vec3 dir, float rad) {
    float PdotD = dot(pos, dir);
    float delta = PdotD * PdotD + rad * rad - dot(pos, pos);
    if (delta < 0.0) return vec2(-1.0);
    delta = sqrt(delta);
    return vec2(-delta, delta) - PdotD;
}

// Domain-warped noise detail (DerivativeMain GetNoiseDetail)
float getNoiseDetail(vec3 worldDir) {
    vec3 dir = worldDir * 48.0;
    vec3 wind = vec3(2e-3, 2e-4, 1e-3) * (uTime * uCloudTimeScale);
    float pnoise = cloudNoiseSharp(dir - wind);       dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 2.0);             dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 4.0) * 0.5;       dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 8.0) * 0.25;      dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 16.0) * 0.125;    dir += pnoise * 1e-3 - wind;
    return pnoise - 0.15;
}

// ============================================================
// PLANAR CLOUDS (Cirrus at ~7000m)
// ============================================================

vec4 evaluatePlanarClouds(vec3 ray, float LdotV, float dayFactor, float moonVis, vec3 skyRadiance,
                           LightingEnvironment env) {
    if ((ray.y < 0.0 && uCameraPos.y < uPlanarCloudAltitude) ||
        (ray.y > 0.0 && uCameraPos.y > uPlanarCloudAltitude)) {
        return vec4(0.0);
    }

    float tPlane = (uPlanarCloudAltitude - uCameraPos.y) / ray.y;
    if (tPlane <= 0.0 || tPlane > 300000.0 - 60000.0 * clamp(uCloudWetness, 0.0, 1.0)) return vec4(0.0);

    vec2 worldPos = uCameraPos.xz + ray.xz * tPlane;
    worldPos /= 1.0 + length(worldPos - uCameraPos.xz) * 5e-6;

    float coverage = clamp(uPlanarCloudCoverage + uCloudWetness * 0.2, 0.05, 0.95);
    float density = cirrusCloudDensity(worldPos, coverage);
    if (density < 1e-5) return vec4(0.0);

    float powder = (1.0 - exp(-density * 2.4)) * 0.7 / (1.0 - (1.0 - exp(-density * 2.4)) * 0.7 + 0.001);
    vec4 phases = multiLobePhase(LdotV, uCloudWetness);
    float phase = dot(phases, vec4(1.0));

    // Sun/moon from LightingEnvironment (SkyCapture metadata).
    // DerivativeMain PlanarClouds.glsl:245: sunlightEnergy * 1.2e2 * (moonlit ? moonIlluminance : sunIlluminance)
    // Sun and moon share the same scattering multiplier — the phase function handles angular dependence.
    vec3 lightIlluminance = env.sunIlluminance * dayFactor + env.moonIlluminance * moonVis;
    vec3 lightColor = phase * lightIlluminance * 40.0;
    // Sky ambient from LightingEnvironment instead of CPU uSkyAmbientColor
    vec3 skyAmb = mix(env.skyHorizonAvg, env.skyZenith, 0.3);
    lightColor += skyAmb * 0.25;
    lightColor *= 1.0 - uCloudWetness * 0.8;

    float opacity = 1.0 - exp(-density * 1.6 * uPlanarCloudDensity);
    float atmosFade = exp(-tPlane * (0.02 + uCloudWetness * 0.12) / max(uPlanarCloudAltitude, 1.0));

    vec3 scattering = lightColor * powder * opacity;
    vec3 color = scattering * atmosFade + skyRadiance * opacity * (1.0 - atmosFade);
    return vec4(color, opacity);
}

// ============================================================
// CIRROCUMULUS CLOUDS (lower planar layer with curl noise)
// ============================================================

vec4 evaluateCirrocumulusClouds(vec3 ray, float LdotV, float dayFactor, float moonVis, float jitter, vec3 skyRadiance, LightingEnvironment env) {
    float altitude = uPlanarCloudAltitude * 0.7; // below cirrus
    if ((ray.y < 0.0 && uCameraPos.y < altitude) ||
        (ray.y > 0.0 && uCameraPos.y > altitude)) {
        return vec4(0.0);
    }

    float tPlane = (altitude - uCameraPos.y) / ray.y;
    if (tPlane <= 0.0 || tPlane > 300000.0 - 60000.0 * clamp(uCloudWetness, 0.0, 1.0)) return vec4(0.0);

    vec2 worldPos = uCameraPos.xz + ray.xz * tPlane;
    float density = cirrocumulusDensity(worldPos);
    if (density < 1e-5) return vec4(0.0);

    // Sun optical depth march (3 steps, exponential growth)
    vec3 sunDir = normalize(uSunDirection);
    float rayLength = 60.0;
    vec2 rayPos = worldPos;
    float opticalDepth = 0.0;

    for (int i = 0; i < 3; ++i) {
        vec2 samplePos = rayPos + sunDir.xz * rayLength * (jitter + 0.5);
        float d = cirrocumulusDensity(samplePos);
        if (d > 1e-4) opticalDepth += d * rayLength;
        rayLength *= 2.0;
    }

    vec4 phases = multiLobePhase(LdotV, uCloudWetness);
    float sunlightEnergy = exp(-opticalDepth * 1.0) * phases.x
                         + exp(-opticalDepth * 0.4) * phases.y
                         + exp(-opticalDepth * 0.15) * phases.z
                         + exp(-opticalDepth * 0.05) * phases.w;

    // Sky light march (2 steps)
    rayLength = 100.0;
    float skyOD = 0.0;
    for (int i = 0; i < 2; ++i) {
        vec2 samplePos = worldPos + vec2(0.0, rayLength * (jitter + 0.5));
        float d = cirrocumulusDensity(samplePos);
        if (d > 1e-4) skyOD += d * rayLength;
        rayLength *= 2.0;
    }
    float skyEnergy = exp(-skyOD * 0.15) + 0.2 * exp(-skyOD * 0.03);

    float powder = (1.0 - exp(-density * 600.0)) * 0.75 / (1.0 - (1.0 - exp(-density * 600.0)) * 0.75 + 0.001);

    vec3 sunIllum = env.sunIlluminance * dayFactor + env.moonIlluminance * moonVis;
    vec3 scattering = sunlightEnergy * 120.0 * sunIllum;
    scattering += skyEnergy * 0.3 * env.skyIlluminance;
    scattering *= 1.0 - uCloudWetness * 0.7;

    float opacity = 1.0 - exp(-density * 0.02 * 1.0 * tPlane);
    float atmosFade = exp(-tPlane * (0.02 + uCloudWetness * 0.12) / max(altitude, 1.0));

    vec3 color = scattering * powder * opacity;
    color = color * atmosFade + skyRadiance * opacity * (1.0 - atmosFade);
    return vec4(color, opacity);
}

// ============================================================
// VOLUMETRIC CLOUDS (Cumulus layer)
// ============================================================

// Sun optical depth with exponential-growth sampling (4 steps)
float sunOcclusionAt(vec3 pos, float height01, float weatherCoverage, float lightNoise) {
    vec3 sunDir = normalize(uSunDirection);
    float opticalDepth = 0.0;
    float stepLen = max(uCloudThickness, 1.0) * 0.05;

    for (int i = 0; i < 4; ++i) {
        vec3 samplePos = pos + sunDir * stepLen * (lightNoise + 0.5);
        float h = clamp((samplePos.y - uCloudHeight) / max(uCloudThickness, 1.0), 0.0, 1.0);
        float d = cloudDensityAt(samplePos, h, weatherCoverage, 1.0);
        opticalDepth += d;
        stepLen *= 2.0;
    }
    return opticalDepth * stepLen * 0.12;
}

// Sky light optical depth (2 steps upward, DerivativeMain CloudVolumeSkyLightOD)
float skyLightOcclusionAt(vec3 pos, float height01, float weatherCoverage, float lightNoise) {
    float rayLength = max(uCloudThickness, 1.0) * 0.1;
    float opticalDepth = 0.0;

    for (int i = 0; i < 2; ++i) {
        vec3 samplePos = pos + vec3(0.0, rayLength * (lightNoise + 0.5), 0.0);
        float h = clamp((samplePos.y - uCloudHeight) / max(uCloudThickness, 1.0), 0.0, 1.0);
        float d = cloudDensityAt(samplePos, h, weatherCoverage, 1.0);
        opticalDepth += d;
        rayLength *= 2.0;
    }
    return opticalDepth * rayLength * 0.04;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 targetPos = reconstructWorldPosition(vTexCoord, depth >= 0.9999 ? 1.0 : depth);
    vec3 ray = normalize(targetPos - uCameraPos);

    // --- Lighting environment from SkyCapture ---
    LightingEnvironment env = getLightingEnvironment(uSkyCaptureTex);

    // Lightning flash: boost cloud illumination so flash lights up clouds.
    env.sunIlluminance *= 1.0 + uLightningFlash * 4.0;
    env.moonIlluminance *= 1.0 + uLightningFlash * 4.0;

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float day = clamp(uSkyIntensity, 0.0, 1.0);
    float moonVis = clamp(uMoonVisibility, 0.0, 1.0) * (1.0 - day);
    float eyeAltitude = max(uCameraPos.y, 0.0) + 100.0;

    vec3 skyRadiance = sampleSkyRadiance(uSkyCaptureTex, ray);
    float LdotV = dot(ray, sunDir);
    float moonLdotV = dot(ray, moonDir);
    float jitter = sampleCloudNoise(vTexCoord * 23.0 + (uTime * uCloudTimeScale) * 0.01);

    // ---- Planar clouds (cirrus layer) ----
    vec4 planarResult = evaluatePlanarClouds(ray, LdotV, day, moonVis, skyRadiance, env);
    float planarTransmittance = 1.0 - planarResult.a;

    // ---- Cirrocumulus planar layer ----
    vec4 cirroResult = evaluateCirrocumulusClouds(ray, LdotV, day, moonVis, jitter, skyRadiance, env);
    // Composite cirrocumulus behind cirrus
    planarResult.rgb += cirroResult.rgb * planarTransmittance;
    planarTransmittance *= 1.0 - cirroResult.a;

    // ---- Volumetric clouds (cumulus layer) ----
    float cloudBottom = uCloudHeight;
    float cloudThickness = max(uCloudThickness, 1.0);
    float cloudTop = cloudBottom + cloudThickness;

    vec3 cloudColor = vec3(0.0);
    float transmittance = 1.0;

    // Sphere-intersection ray setup (DerivativeMain curved-earth approach)
    vec3 planeOrigin = vec3(0.0, planetRadius + uCameraPos.y, 0.0);
    vec2 bottomIntersection = raySphereIntersection(planeOrigin, ray, planetRadius + cloudBottom);
    vec2 topIntersection = raySphereIntersection(planeOrigin, ray, planetRadius + cloudTop);

    float startT, endT;
    if (uCameraPos.y > cloudTop) {
        startT = topIntersection.x;
        endT = bottomIntersection.x;
    } else if (uCameraPos.y < cloudBottom) {
        startT = bottomIntersection.y;
        endT = topIntersection.y;
    } else {
        // Camera inside cloud layer
        startT = 0.0;
        endT = max(bottomIntersection.x, topIntersection.y);
    }

    startT = max(startT, 0.0);
    endT = max(endT, 0.0);

    // In-cloud range detection
    float rayRange = (1.0 - clamp((uCameraPos.y - cloudTop) * 0.1, 0.0, 1.0))
                   * (1.0 - clamp((cloudBottom - uCameraPos.y) * 0.1, 0.0, 1.0));
    float rayDist = bottomIntersection.y >= 0.0 && uCameraPos.y > cloudBottom
                  ? bottomIntersection.x : topIntersection.y;
    startT *= 1.0 - rayRange;
    endT = mix(endT, rayDist, rayRange);

    if (endT > startT && endT > 0.0) {
        // DerivativeMain: steps fade with ray angle
        int steps = 32;
        steps = int(mix(float(steps), float(steps) / 1.6, abs(ray.y)));

        float weatherCoverage = clamp(uCloudCoverage * 2.8 + 0.2 + uCloudWetness * 0.3, 0.8, 1.5);
        float rayDistance = clamp(endT - startT, 0.0, 20000.0);
        float stepLength = rayDistance / float(steps);

        vec4 phases = multiLobePhase(LdotV, uCloudWetness);
        float sunVisibility = smoothstep(-0.06, 0.18, sunDir.y) * day;

        // Domain-warped noise detail
        float noiseDetail = getNoiseDetail(ray);

        float scatteringSun = 0.0;
        float scatteringSky = 0.0;
        float lastCloudDistance = startT;

        for (int i = 0; i < steps; ++i) {
            if (transmittance < 0.05) break; // DerivativeMain minTransmittance

            float t = startT + (float(i) + jitter) * stepLength;
            vec3 pos = uCameraPos + ray * t;
            float height01 = clamp((pos.y - cloudBottom) / cloudThickness, 0.0, 1.0);

            float dist = length(pos - uCameraPos);
            float detailBlend = mix(noiseDetail, 1.0, exp(-dist * 0.001) * 0.8);
            float density = cloudDensityAt(pos, height01, weatherCoverage, detailBlend);
            if (density <= 0.001) continue;
            lastCloudDistance = dist;

            // Sun optical depth — DerivativeMain Deferred1.glsl:219-222
            float sunOD = sunOcclusionAt(pos, height01, weatherCoverage, jitter);
            float sunlightEnergy = exp(-sunOD * 2.0) * phases.x
                                 + exp(-sunOD * 0.8) * phases.y
                                 + exp(-sunOD * 0.3) * phases.z
                                 + exp(-sunOD * 0.1) * phases.w;

            // Sky light optical depth
            float skyOD = skyLightOcclusionAt(pos, height01, weatherCoverage, jitter);
            float skyEnergy = exp(-skyOD) + exp(-skyOD * 0.1) * 0.1;

            // Beer-Powder scattering — DerivativeMain Deferred1.glsl:216
            float powder = (1.0 - exp(-density * 36.0)) * 0.82;
            powder /= 1.0 - powder + 0.001;

            float stepTransmittance = exp(-density * stepLength * 0.04);
            float cloudSample = powder * transmittance * (1.0 - stepTransmittance);

            scatteringSun += sunlightEnergy * cloudSample;
            scatteringSky += skyEnergy * cloudSample;
            transmittance *= stepTransmittance;
        }

        // DerivativeMain Deferred1.glsl:238: only composite if transmittance < 1 - minTransmittance
        if (transmittance < 0.95) {
            float opacity = clamp(1.0 - transmittance, 0.0, 1.0);

            // DerivativeMain Deferred1.glsl:239-241: moonlit hard cutoff for illuminance
            bool moonlit = sunDir.y < -0.04;
            vec3 sunIllum = env.sunIlluminance * sunVisibility;
            vec3 moonIllum = env.moonIlluminance * moonVis;
            vec3 lightIlluminance = moonlit ? moonIllum : sunIllum;

            // DerivativeMain VolumetricClouds.glsl:60-68: rain cloud lighting = 0.3
            float wetness = clamp(uCloudWetness, 0.0, 1.0);
            float cloudSunlighting = mix(1.0, 0.3, wetness);
            float cloudSkylighting = mix(1.0, 0.3, wetness);

            vec3 scattering = scatteringSun * 22.0 * lightIlluminance * cloudSunlighting;
            scattering += scatteringSky * 0.15 * env.skyIlluminance * cloudSkylighting;

            // DerivativeMain Deferred1.glsl:271-277: distant cloud scattering
            // fades back toward raw sky radiance by cloud opacity.
            float atmosFade = exp(-lastCloudDistance * (0.2 + 0.1 * wetness) * 1e-4);
            cloudColor = scattering * atmosFade + skyRadiance * opacity * (1.0 - atmosFade);
        }
    }

    // ---- Combine planar + volumetric clouds (premultiplied alpha, transmittance in .a) ----
    // DerivativeMain Deferred1.glsl:378: vec4(scattering, transmittance)
    vec3 finalColor = cloudColor + planarResult.rgb * transmittance;
    float finalTransmittance = transmittance * planarTransmittance;

    FragColor = vec4(max(finalColor, vec3(0.0)), finalTransmittance);
}
