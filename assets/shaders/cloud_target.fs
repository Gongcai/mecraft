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
uniform float uSkyWetness;
uniform float uSurfaceWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uPrecipitation;
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

const float PHI = 1.61803398875;
const float GOLDEN_ANGLE = 6.28318530718 / (PHI + 1.0);
const int noiseTextureResolution = 256;
const float noiseTexturePixelSize = 1.0 / float(noiseTextureResolution);

// DerivativeMain planet radius for sphere-intersection ray setup
const float planetRadius = 6371000.0;

vec3 saturate3(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }
float curve(float x) { return x * x * (3.0 - 2.0 * x); }
vec3 curve3(vec3 x) { return x * x * (3.0 - 2.0 * x); }
float cube(float x) { return x * x * x; }

vec3 reconstructWorldPosition(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = uInvViewProj * clip;
    return world.xyz / max(world.w, 0.00001);
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float sampleCloudNoise(vec2 p) {
    if (!uNoiseEnabled) {
        return hash12(p);
    }
    vec4 n0 = texture(uNoiseTex, p);
    vec4 n1 = texture(uNoiseTex, p * 2.37 + vec2(0.17, -0.29));
    return n0.r * 0.62 + n1.g * 0.38;
}

// DerivativeMain-style 3D noise: Z-slice technique using 2D noise texture
float get3DNoiseSmooth(vec3 position) {
    vec3 p = floor(position);
    vec3 b = curve3(position - p);
    vec2 uv = p.xy + b.xy + 97.0 * p.z;
    vec2 coord = (uv + 0.5) * noiseTexturePixelSize;
    vec2 rg = texture(uNoiseTex, coord).xy;
    return mix(rg.x, rg.y, b.z);
}

float get3DNoise(vec3 position) {
    vec3 p = floor(position);
    vec3 f = position - p;
    f = clamp(f, vec3(0.0), vec3(1.0));
    vec2 uv = p.xy + f.xy + p.z * 97.0;
    vec2 coord = (uv + 0.5) * noiseTexturePixelSize;
    vec2 noiseSample = texture(uNoiseTex, coord).xy;
    return mix(noiseSample.x, noiseSample.y, f.z);
}

float cornetteShanksPhase(float cosTheta, float g) {
    float gg = g * g;
    float mu2 = cosTheta * cosTheta;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (3.0 * (1.0 - gg) * (1.0 + mu2)) /
           (8.0 * atmPi * (2.0 + gg) * denom * sqrt(denom));
}

vec4 multiLobePhase(float cosTheta) {
    float forward = atmHenyeyGreensteinPhase(cosTheta, 0.6);
    float backward = atmHenyeyGreensteinPhase(cosTheta, -0.35);
    float peak = cornetteShanksPhase(cosTheta, 0.85);
    float peak2 = cornetteShanksPhase(cosTheta, 0.5);
    return vec4(forward, backward, peak, peak2) * vec4(0.55, 0.20, 0.15, 0.10);
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
    vec3 wind = vec3(2e-3, 2e-4, 1e-3) * uTime;
    float pnoise = get3DNoise(dir - wind);       dir += pnoise * 1e-3 - wind;
    pnoise += get3DNoise(dir * 2.0);             dir += pnoise * 1e-3 - wind;
    pnoise += get3DNoise(dir * 4.0) * 0.5;       dir += pnoise * 1e-3 - wind;
    pnoise += get3DNoise(dir * 8.0) * 0.25;      dir += pnoise * 1e-3 - wind;
    pnoise += get3DNoise(dir * 16.0) * 0.125;    dir += pnoise * 1e-3 - wind;
    return pnoise - 0.15;
}

// ============================================================
// PLANAR CLOUDS (Cirrus at ~7000m)
// ============================================================

float cirrusCloudDensity(vec2 worldPos, float coverage) {
    vec2 wind = vec2(uTime * 0.0003, -uTime * 0.0002);
    vec2 pos = worldPos * 4e-5 - wind;

    float noise = 0.0;
    float amplitude = 0.5;
    mat2 goldenRot = mat2(cos(GOLDEN_ANGLE), -sin(GOLDEN_ANGLE),
                          sin(GOLDEN_ANGLE), cos(GOLDEN_ANGLE));

    for (int i = 0; i < 5; ++i) {
        noise += sampleCloudNoise(pos) * amplitude;
        pos = goldenRot * 3.2 * pos;
        amplitude *= 0.43;
    }

    return max(noise * coverage - 0.2, 0.0);
}

vec4 evaluatePlanarClouds(vec3 ray, float LdotV, float dayFactor, float moonVis,
                           LightingEnvironment env) {
    if ((ray.y < 0.0 && uCameraPos.y < uPlanarCloudAltitude) ||
        (ray.y > 0.0 && uCameraPos.y > uPlanarCloudAltitude)) {
        return vec4(0.0);
    }

    float tPlane = (uPlanarCloudAltitude - uCameraPos.y) / ray.y;
    if (tPlane <= 0.0 || tPlane > 300000.0 - 60000.0 * clamp(uSkyWetness, 0.0, 1.0)) return vec4(0.0);

    vec2 worldPos = uCameraPos.xz + ray.xz * tPlane;
    worldPos /= 1.0 + length(worldPos - uCameraPos.xz) * 5e-6;

    float coverage = clamp(uPlanarCloudCoverage + uCloudWetness * 0.2, 0.05, 0.95);
    float density = cirrusCloudDensity(worldPos, coverage);
    if (density < 1e-5) return vec4(0.0);

    float powder = (1.0 - exp(-density * 2.4)) * 0.7 / (1.0 - (1.0 - exp(-density * 2.4)) * 0.7 + 0.001);
    vec4 phases = multiLobePhase(LdotV);
    float phase = dot(phases, vec4(1.0));

    // Sun/moon from LightingEnvironment (SkyCapture metadata).
    // DerivativeMain PlanarClouds.glsl:245: sunlightEnergy * 1.2e2 * (moonlit ? moonIlluminance : sunIlluminance)
    // Sun and moon share the same scattering multiplier — the phase function handles angular dependence.
    vec3 lightIlluminance = env.sunIlluminance * dayFactor + env.moonIlluminance * moonVis;
    vec3 lightColor = phase * lightIlluminance * 40.0;
    // Sky ambient from LightingEnvironment instead of CPU uSkyAmbientColor
    vec3 skyAmb = mix(env.skyHorizonAvg, env.skyZenith, 0.3);
    lightColor += skyAmb * 0.25;
    lightColor *= 1.0 - uSkyWetness * 0.8;

    float opacity = 1.0 - exp(-density * 4.0 * uPlanarCloudDensity);
    float atmosFade = exp(-tPlane * (0.02 + uSkyWetness * 0.12) / max(uPlanarCloudAltitude, 1.0));
    opacity *= atmosFade;

    vec3 color = lightColor * powder * opacity;
    return vec4(color, opacity);
}

// ============================================================
// CIRROCUMULUS CLOUDS (lower planar layer with curl noise)
// ============================================================

float cirrocumulusDensity(vec2 worldPos) {
    vec2 wind = vec2(uTime * 0.0003, -uTime * 0.0002);
    worldPos /= 1.0 + length(worldPos - uCameraPos.xz) * 2e-5;
    vec2 position = worldPos * 1e-4 - wind;

    float baseCoverage = curve(texture(uNoiseTex, position * 0.08).z * 0.7 + 0.1);
    baseCoverage *= max(1.07 - texture(uNoiseTex, position * 0.003).y * 1.4, 0.0);

    vec2 curl = texture(uNoiseTex, position * 0.05).xy * 0.04;
    curl += texture(uNoiseTex, position * 0.1).xy * 0.02;
    position += curl;

    float noise = 0.5 * texture(uNoiseTex, position * vec2(0.4, 0.16)).z;
    noise += texture(uNoiseTex, position * 0.9).z - 0.24;
    noise = clamp(noise, 0.0, 1.0);

    noise *= clamp((baseCoverage + 0.5 - 0.6) * 0.9, 0.0, 0.14);
    if (noise < 1e-6) return 0.0;

    position.x += noise * 0.2;
    noise += 0.02 * texture(uNoiseTex, position * 3.0).z;
    noise += 0.01 * texture(uNoiseTex, position * 5.0 + curl).z - 0.05;

    return cube(clamp(noise * 4.0, 0.0, 1.0));
}

vec4 evaluateCirrocumulusClouds(vec3 ray, float LdotV, float dayFactor, float moonVis, float jitter, LightingEnvironment env) {
    float altitude = uPlanarCloudAltitude * 0.7; // below cirrus
    if ((ray.y < 0.0 && uCameraPos.y < altitude) ||
        (ray.y > 0.0 && uCameraPos.y > altitude)) {
        return vec4(0.0);
    }

    float tPlane = (altitude - uCameraPos.y) / ray.y;
    if (tPlane <= 0.0 || tPlane > 300000.0 - 60000.0 * clamp(uSkyWetness, 0.0, 1.0)) return vec4(0.0);

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

    vec4 phases = multiLobePhase(LdotV);
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
    scattering *= 1.0 - uSkyWetness * 0.7;

    float opacity = 1.0 - exp(-density * 0.02 * 1.0 * tPlane);
    float atmosFade = exp(-tPlane * (0.02 + uSkyWetness * 0.12) / max(altitude, 1.0));
    opacity *= atmosFade;

    vec3 color = scattering * powder * opacity;
    return vec4(color, opacity);
}

// ============================================================
// VOLUMETRIC CLOUDS (Cumulus layer)
// ============================================================

float cloudDensityAt(vec3 worldPos, float normalizedHeight, float weatherCoverage, float noiseDetail) {
    vec3 wind = vec3(2e-3, 2e-4, 1e-3) * uTime;
    float noiseScale = 4e-4 + 6e-5 * uCloudWetness;
    vec3 position = worldPos * noiseScale - wind;

    // Local coverage
    float localCoverage = texture(uNoiseTex, worldPos.xz * 2e-7 - wind.xz * 2e-3 + 0.5).y;
    localCoverage = clamp(localCoverage * 3.0 + uCloudWetness - 0.4, 0.0, 1.0) * 0.5 + 0.5;
    if (localCoverage < 0.1) return 0.0;

    float density = noiseDetail * 0.03;
    float weight = 0.5;
    const float octWeight = 0.5;
    const float octScale = 3.0;

    for (int i = 0; i < 4; ++i) {
        density += weight * get3DNoiseSmooth(position);
        position = position * octScale - wind;
        weight *= octWeight;
    }
    density += octWeight / octScale / 4.0;
    if (density < 1e-6) return 0.0;

    density *= localCoverage;

    float heightAttenuation = clamp(normalizedHeight * 6.6, 0.0, 1.0)
                            * clamp((1.0 - normalizedHeight) * (2.0 + uCloudWetness), 0.0, 1.0);

    if (weatherCoverage != 1.0) {
        density = clamp((density - 1.0 + weatherCoverage) / weatherCoverage, 0.0, 1.0);
    }

    density *= heightAttenuation * 1.9;
    density -= heightAttenuation * 0.9 + normalizedHeight * 0.5 + 0.1;

    return clamp(density * 3.0 * uCloudDensity, 0.0, 1.0);
}

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

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float day = clamp(uSkyIntensity, 0.0, 1.0);
    float moonVis = clamp(uMoonVisibility, 0.0, 1.0) * (1.0 - day);
    float eyeAltitude = max(uCameraPos.y, 0.0) + 100.0;

    vec3 atmos = sampleAtmosphere(ray, sunDir, moonDir, eyeAltitude, day, moonVis);
    float LdotV = dot(ray, sunDir);
    float moonLdotV = dot(ray, moonDir);
    float jitter = sampleCloudNoise(vTexCoord * 23.0 + uTime * 0.01);

    // ---- Planar clouds (cirrus layer) ----
    vec4 planarResult = evaluatePlanarClouds(ray, LdotV, day, moonVis, env);
    float planarTransmittance = 1.0 - planarResult.a;

    // ---- Cirrocumulus planar layer ----
    vec4 cirroResult = evaluateCirrocumulusClouds(ray, LdotV, day, moonVis, jitter, env);
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

        vec4 phases = multiLobePhase(LdotV);
        float moonPhaseVal = dot(multiLobePhase(moonLdotV), vec4(1.0));
        float sunVisibility = smoothstep(-0.06, 0.18, sunDir.y) * day;

        // Domain-warped noise detail
        float noiseDetail = getNoiseDetail(ray);

        float scatteringSun = 0.0;
        float scatteringSky = 0.0;

        for (int i = 0; i < steps; ++i) {
            if (transmittance < 0.01) break;

            float t = startT + (float(i) + jitter) * stepLength;
            vec3 pos = uCameraPos + ray * t;
            float height01 = clamp((pos.y - cloudBottom) / cloudThickness, 0.0, 1.0);

            float dist = length(pos - uCameraPos);
            float detailBlend = mix(noiseDetail, 1.0, exp(-dist * 0.001) * 0.8);
            float density = cloudDensityAt(pos, height01, weatherCoverage, detailBlend);
            if (density <= 0.001) continue;

            // Sun optical depth (4-lobe energy conservation)
            float sunOD = sunOcclusionAt(pos, height01, weatherCoverage, jitter);
            float sunlightEnergy = exp(-sunOD * 2.0) * phases.x
                                 + exp(-sunOD * 0.8) * phases.y
                                 + exp(-sunOD * 0.3) * phases.z
                                 + exp(-sunOD * 0.1) * phases.w;

            // Sky light optical depth
            float skyOD = skyLightOcclusionAt(pos, height01, weatherCoverage, jitter);
            float skyEnergy = exp(-skyOD) + exp(-skyOD * 0.1) * 0.1;

            // Beer-Powder scattering
            float powder = (1.0 - exp(-density * 32.0)) * 0.82;
            powder /= 1.0 - powder + 0.001;

            float stepTransmittance = exp(-density * stepLength * 0.04);
            float cloudSample = powder * transmittance * (1.0 - stepTransmittance);

            scatteringSun += sunlightEnergy * cloudSample;
            scatteringSky += skyEnergy * cloudSample;
            transmittance *= stepTransmittance;
        }

        float opacity = clamp(1.0 - transmittance, 0.0, 1.0);
        float distanceFade = exp(-startT * (0.00020 + 0.00018 * clamp(uSkyWetness, 0.0, 1.0)));
        opacity *= distanceFade;

        // Compose with SkyCapture illuminance (unified source)
        // DerivativeMain Deferred1.glsl:240-241: sun*22.0, sky*0.15
        vec3 sunIllum = env.sunIlluminance * day + env.moonIlluminance * moonVis;
        vec3 scattering = scatteringSun * 22.0 * sunIllum * sunVisibility;
        scattering += scatteringSky * 0.15 * env.skyIlluminance;

        // Weather darkening
        scattering = mix(scattering, scattering * vec3(0.68, 0.75, 0.86),
                        clamp(uSkyWetness, 0.0, 1.0) * 0.55);

        cloudColor = scattering;
        cloudColor += atmos * opacity * mix(0.5, 0.8, clamp(uHorizonScatterStrength, 0.0, 1.0));
        transmittance = 1.0 - opacity;
    }

    // ---- Combine planar + volumetric clouds ----
    vec3 finalColor = cloudColor + planarResult.rgb * transmittance;
    float finalOpacity = clamp(1.0 - transmittance * planarTransmittance, 0.0, 1.0);

    FragColor = vec4(max(finalColor, vec3(0.0)), finalOpacity);
}
