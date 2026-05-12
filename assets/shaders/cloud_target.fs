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
uniform float uWeatherMist;
uniform float uWeatherWetness;
uniform float uWeatherStorm;
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

#include "atmosphere_lut.glsl"

const float PHI = 1.61803398875;
const float GOLDEN_ANGLE = 6.28318530718 / (PHI + 1.0);
const int noiseTextureResolution = 256;
const float noiseTexturePixelSize = 1.0 / float(noiseTextureResolution);

vec3 saturate3(vec3 x) { return clamp(x, vec3(0.0), vec3(1.0)); }
float curve(float x) { return x * x * (3.0 - 2.0 * x); }
vec3 curve3(vec3 x) { return x * x * (3.0 - 2.0 * x); }

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

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
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
// UV offset by Z*97.0 to sample different region per Z slice
// R/G channels interpolated by Z fractional part
float get3DNoise(vec3 position) {
    vec3 p = floor(position);
    vec3 f = position - p;
    f = clamp(f, vec3(0.0), vec3(1.0));

    vec2 uv = p.xy + f.xy + p.z * 97.0;
    vec2 coord = (uv + 0.5) * noiseTexturePixelSize;
    vec2 noiseSample = texture(uNoiseTex, coord).xy;
    return mix(noiseSample.x, noiseSample.y, f.z);
}

float get3DNoiseSmooth(vec3 position) {
    vec3 p = floor(position);
    vec3 b = curve3(position - p);

    vec2 uv = p.xy + b.xy + 97.0 * p.z;
    vec2 coord = (uv + 0.5) * noiseTexturePixelSize;
    vec2 rg = texture(uNoiseTex, coord).xy;

    return mix(rg.x, rg.y, b.z);
}

float sampleNoise3D(vec3 p) {
    if (!uNoiseEnabled) {
        return hash12(p.xy + p.z * 97.0);
    }
    vec2 texCoord = fract(p.xy + p.z * 97.0 / 256.0);
    return texture(uNoiseTex, texCoord).r;
}

float cornetteShanksPhase(float cosTheta, float g) {
    float gg = g * g;
    float mu2 = cosTheta * cosTheta;
    float denom = 1.0 + gg - 2.0 * g * cosTheta;
    return (3.0 * (1.0 - gg) * (1.0 + mu2)) /
           (8.0 * atmPi * (2.0 + gg) * denom * sqrt(denom));
}

float multiLobePhase(float cosTheta) {
    float forward = atmHenyeyGreensteinPhase(cosTheta, 0.6);
    float backward = atmHenyeyGreensteinPhase(cosTheta, -0.35);
    float peak = cornetteShanksPhase(cosTheta, 0.85);
    return forward * 0.55 + backward * 0.20 + peak * 0.25;
}

vec3 sampleAtmosphere(vec3 ray, vec3 sunDir, vec3 moonDir, float eyeAltitude, float dayFactor, float moonVis) {
    vec3 transmittance;
    vec3 sunSky = atmGetSkyRadianceForLight(eyeAltitude, ray, sunDir, transmittance);
    vec3 moonSky = atmGetSkyRadianceForLight(eyeAltitude, ray, moonDir, transmittance) *
                   moonVis * (1.0 - dayFactor) * 0.28;
    return max(sunSky + moonSky, vec3(0.0));
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

vec4 evaluatePlanarClouds(vec3 ray, float dist, float LdotV, float dayFactor, float moonVis) {
    if (ray.y < 0.02 && uCameraPos.y < uPlanarCloudAltitude) return vec4(0.0);
    if (ray.y > -0.02 && uCameraPos.y > uPlanarCloudAltitude + 500.0) return vec4(0.0);

    float tPlane = (uPlanarCloudAltitude - uCameraPos.y) / max(abs(ray.y), 0.02);
    if (tPlane < 0.0 || tPlane > 300000.0) return vec4(0.0);

    vec2 worldPos = uCameraPos.xz + ray.xz * tPlane;
    worldPos /= 1.0 + length(worldPos - uCameraPos.xz) * 5e-6;

    float coverage = clamp(uPlanarCloudCoverage + uWeatherWetness * 0.2 + uWeatherStorm * 0.3, 0.05, 0.95);
    float density = cirrusCloudDensity(worldPos, coverage);

    if (density < 1e-5) return vec4(0.0);

    float powder = (1.0 - exp(-density * 2.4)) * 0.7 / (1.0 - (1.0 - exp(-density * 2.4)) * 0.7 + 0.001);
    float phase = multiLobePhase(LdotV);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);

    vec3 lightColor = phase * uSunLightColor * dayFactor * 40.0;
    lightColor += phase * uMoonLightColor * moonVis * 12.0;
    lightColor += uSkyAmbientColor * 0.25;
    lightColor *= 1.0 - uWeatherWetness * 0.8;

    float opacity = 1.0 - exp(-density * 4.0 * uPlanarCloudDensity);
    float atmosFade = exp(-tPlane * (0.02 + uWeatherWetness * 0.12) / max(uPlanarCloudAltitude, 1.0));
    opacity *= atmosFade;

    vec3 color = lightColor * powder * opacity;
    return vec4(color, opacity);
}

// ============================================================
// VOLUMETRIC CLOUDS (Cumulus layer)
// ============================================================

float cloudDensityAt(vec3 worldPos, float normalizedHeight, float weatherCoverage) {
    // Direct port of DerivativeMain CloudVolumeDensity
    vec3 wind = vec3(2e-3, 2e-4, 1e-3) * uTime;
    float noiseScale = 4e-4 + 6e-5 * uWeatherWetness;

    vec3 position = worldPos * noiseScale - wind;

    float density = 0.03; // base offset (noiseDetail * 0.03 equivalent)
    float weight = 0.5;
    const float octWeight = 0.5;
    const float octScale = 3.0;
    const int octaves = 4;

    for (int i = 0; i < octaves; ++i) {
        density += weight * get3DNoiseSmooth(position);
        position = position * octScale - wind;
        weight *= octWeight;
    }

    density += octWeight / octScale / float(octaves);

    if (density < 1e-6) return 0.0;

    // Local coverage (DerivativeMain CLOUD_LOCAL_COVERAGE)
    float localCoverage = texture(uNoiseTex, worldPos.xz * 2e-7 - wind.xz * 2e-3 + 0.5).y;
    localCoverage = clamp(localCoverage * 3.0 + uWeatherWetness - 0.4, 0.0, 1.0) * 0.5 + 0.5;
    if (localCoverage < 0.1) return 0.0;
    density *= localCoverage;

    // Height attenuation (exact DerivativeMain formula)
    float heightAttenuation = clamp(normalizedHeight * 6.6, 0.0, 1.0)
                            * clamp((1.0 - normalizedHeight) * (2.0 + uWeatherWetness), 0.0, 1.0);

    // Coverage threshold
    if (weatherCoverage != 1.0) {
        density = clamp((density - 1.0 + weatherCoverage) / weatherCoverage, 0.0, 1.0);
    }

    // Key formula: bias + amplify (DerivativeMain lines 123-126)
    density *= heightAttenuation * 1.9;
    density -= heightAttenuation * 0.9 + normalizedHeight * 0.5 + 0.1;

    return clamp(density * 3.0 * uCloudDensity, 0.0, 1.0);
}

float sunOcclusionAt(vec3 pos, float height01, float weatherCoverage, float lightNoise) {
    // Exponential-growth sampling for sun optical depth
    vec3 sunDir = normalize(uSunDirection);
    float opticalDepth = 0.0;
    float stepLen = uCloudThickness * 0.05;

    for (int i = 0; i < 4; ++i) {
        vec3 samplePos = pos + sunDir * stepLen;
        float h = clamp((samplePos.y - uCloudHeight) / max(uCloudThickness, 1.0), 0.0, 1.0);
        float d = cloudDensityAt(samplePos, h, weatherCoverage);
        opticalDepth += d;
        stepLen *= 2.0;
    }
    return opticalDepth * 0.12;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 targetPos = reconstructWorldPosition(vTexCoord, depth >= 0.9999 ? 1.0 : depth);
    vec3 ray = normalize(targetPos - uCameraPos);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float day = clamp(uSkyIntensity, 0.0, 1.0);
    float moonVis = clamp(uMoonVisibility, 0.0, 1.0) * (1.0 - day);
    float eyeAltitude = max(uCameraPos.y, 0.0) + 100.0;

    vec3 atmos = sampleAtmosphere(ray, sunDir, moonDir, eyeAltitude, day, moonVis);
    vec3 sky = texture(uSkyCaptureTex, projectSky(ray)).rgb;
    vec3 horizon = mix(sky, uHorizonScatterColor, clamp(uHorizonScatterStrength, 0.0, 2.0) * 0.22);

    // ---- Planar clouds (cirrus layer) ----
    float LdotV = dot(ray, sunDir);
    float moonLdotV = dot(ray, moonDir);
    vec4 planarResult = evaluatePlanarClouds(ray, 0.0, LdotV, day, moonVis);
    float planarTransmittance = 1.0 - planarResult.a;

    // ---- Volumetric clouds (cumulus layer) ----
    float cloudBottom = uCloudHeight;
    float cloudThickness = max(uCloudThickness, 1.0);
    float cloudTop = cloudBottom + cloudThickness;

    vec3 cloudColor = vec3(0.0);
    float transmittance = 1.0;

    // Only march if ray intersects cloud layer
    if ((ray.y > 0.015 || uCameraPos.y >= cloudBottom) &&
        (ray.y < -0.015 || uCameraPos.y <= cloudTop + 96.0)) {

        float rayY = abs(ray.y) < 0.025 ? (ray.y < 0.0 ? -0.025 : 0.025) : ray.y;
        float tEnter = (cloudBottom - uCameraPos.y) / rayY;
        float tExit = (cloudTop - uCameraPos.y) / rayY;
        float startT = max(min(tEnter, tExit), 0.0);
        float endT = max(tEnter, tExit);

        if (endT > startT) {
            // Remap coverage: our 0-1 range → DerivativeMain 0.8-1.5 range
            float weatherCoverage = clamp(uCloudCoverage * 2.8 + 0.2 + uWeatherWetness * 0.3 + uWeatherStorm * 0.4, 0.8, 1.5);
            float rayDistance = clamp(endT - startT, 0.0, 12000.0);
            int steps = 16; // Increased from 5-7 to 16
            float stepLength = rayDistance / float(steps);

            float jitter = sampleCloudNoise(vTexCoord * 23.0 + uTime * 0.01);
            float phase = multiLobePhase(LdotV);
            float moonPhase = multiLobePhase(moonLdotV);
            float sunVisibility = smoothstep(-0.06, 0.18, sunDir.y) * day;

            for (int i = 0; i < 16; ++i) {
                if (transmittance < 0.01) break;

                float t = startT + (float(i) + jitter) * stepLength;
                vec3 pos = uCameraPos + ray * t;
                float height01 = clamp((pos.y - cloudBottom) / cloudThickness, 0.0, 1.0);
                float density = cloudDensityAt(pos, height01, weatherCoverage);
                if (density <= 0.001) continue;

                // Sun light with exponential-growth occlusion sampling
                float sunOD = sunOcclusionAt(pos, height01, weatherCoverage, jitter);
                float sunlight = exp(-sunOD * mix(3.4, 8.5, clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0)));

                // Beer-Powder scattering
                float beer = exp(-density * stepLength * 0.012);
                float powder = (1.0 - exp(-density * 16.0)) * mix(0.72, 1.15, clamp(1.0 - LdotV, 0.0, 1.0));
                float beerPowder = beer * (0.46 + powder);

                float stepOpacity = 1.0 - exp(-density * stepLength * 0.04);
                stepOpacity = clamp(stepOpacity, 0.0, 0.7);

                vec3 sunlightColor = uSunLightColor * phase * sunVisibility * sunlight * (60.0 + powder * 50.0);
                vec3 moonlightColor = uMoonLightColor * moonPhase * moonVis * (14.0 + powder * 16.0);
                vec3 skylightColor = mix(uSkyAmbientColor, horizon + atmos * 0.16, 0.52) * (0.6 + 0.4 * height01);
                vec3 sampleColor = (sunlightColor + moonlightColor + skylightColor) * beerPowder;
                sampleColor = mix(sampleColor, sampleColor * vec3(0.68, 0.75, 0.86), clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0) * 0.55);

                cloudColor += transmittance * sampleColor * stepOpacity;
                transmittance *= 1.0 - stepOpacity;
            }

            float opacity = clamp(1.0 - transmittance, 0.0, 1.0);
            float distanceFade = exp(-startT * (0.00020 + 0.00018 * clamp(uWeatherWetness, 0.0, 1.0)));
            opacity *= distanceFade * smoothstep(-0.02, 0.12, ray.y);
            cloudColor += atmos * opacity * mix(0.5, 0.8, clamp(uHorizonScatterStrength, 0.0, 1.0));
            cloudColor = mix(cloudColor, horizon * opacity, clamp(uWeatherStorm, 0.0, 1.0) * 0.18);
            transmittance = 1.0 - opacity;
        }
    }

    // ---- Combine planar + volumetric clouds ----
    // Planar clouds are behind volumetric clouds
    vec3 finalColor = cloudColor + planarResult.rgb * transmittance;
    float finalOpacity = clamp(1.0 - transmittance * planarTransmittance, 0.0, 1.0);

    FragColor = vec4(max(finalColor, vec3(0.0)), finalOpacity);
}
