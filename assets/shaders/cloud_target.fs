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

#include "atmosphere_lut.glsl"

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

float mieCloudPhase(float cosTheta) {
    float forward = atmHenyeyGreensteinPhase(cosTheta, 0.62);
    float backward = atmHenyeyGreensteinPhase(cosTheta, -0.32);
    float wide = atmHenyeyGreensteinPhase(cosTheta * 0.45, 0.35);
    return forward * 0.68 + backward * 0.18 + wide * 0.14;
}

vec3 sampleAtmosphere(vec3 ray, vec3 sunDir, vec3 moonDir, float eyeAltitude, float dayFactor, float moonVisibility) {
    vec3 transmittance;
    vec3 sunSky = atmGetSkyRadianceForLight(eyeAltitude, ray, sunDir, transmittance);
    vec3 moonSky = atmGetSkyRadianceForLight(eyeAltitude, ray, moonDir, transmittance) *
                   moonVisibility * (1.0 - dayFactor) * 0.28;
    return max(sunSky + moonSky, vec3(0.0));
}

float cloudDensityAt(vec2 baseUv, float height01, float weatherCoverage) {
    vec2 wind = vec2(uTime * 0.006, -uTime * 0.0035);
    vec2 p = baseUv + wind;
    float localCoverage = sampleCloudNoise(p * 0.08 - wind * 0.02);
    localCoverage = clamp(localCoverage * 2.6 + weatherCoverage - 1.25, 0.0, 1.0);

    float base = sampleCloudNoise(p * 0.42);
    float detail = sampleCloudNoise(p * 1.28 + vec2(7.1, -2.4));
    float curl = sampleCloudNoise(p * 2.75 + vec2(detail * 0.28, -base * 0.18));
    float fbm = base * 0.54 + detail * 0.31 + curl * 0.15;
    float anvilFade = smoothstep(0.02, 0.20, height01) * (1.0 - smoothstep(0.76, 1.0, height01));
    float heightShape = anvilFade * mix(0.72, 1.35, smoothstep(0.14, 0.48, height01));
    float threshold = mix(0.72, 0.36, weatherCoverage);
    float density = clamp((fbm - threshold) / max(1.0 - threshold, 0.001), 0.0, 1.0);
    density *= localCoverage * heightShape;
    density = clamp(density * (1.35 + uWeatherWetness * 0.42 + uWeatherStorm * 0.80), 0.0, 1.0);
    return density;
}

void main() {
    float depth = texture(uDepthTex, vTexCoord).r;
    vec3 targetPos = reconstructWorldPosition(vTexCoord, depth >= 0.9999 ? 1.0 : depth);
    vec3 ray = normalize(targetPos - uCameraPos);

    float cloudBottom = uCloudHeight;
    float cloudThickness = max(uCloudThickness, 1.0);
    float cloudTop = cloudBottom + cloudThickness;
    if ((ray.y <= 0.015 && uCameraPos.y < cloudBottom) || (ray.y >= -0.015 && uCameraPos.y > cloudTop + 96.0)) {
        FragColor = vec4(0.0);
        return;
    }

    float rayY = abs(ray.y) < 0.025 ? (ray.y < 0.0 ? -0.025 : 0.025) : ray.y;
    float tEnter = (cloudBottom - uCameraPos.y) / rayY;
    float tExit = (cloudTop - uCameraPos.y) / rayY;
    float startT = max(min(tEnter, tExit), 0.0);
    float endT = max(tEnter, tExit);
    if (endT <= startT) {
        FragColor = vec4(0.0);
        return;
    }

    float weatherCoverage = clamp(uCloudCoverage + uWeatherMist * 0.12 + uWeatherWetness * 0.22 + uWeatherStorm * 0.34, 0.02, 0.98);
    float rayDistance = clamp(endT - startT, 0.0, 12000.0);
    int steps = ray.y > 0.52 ? 5 : 7;
    float stepLength = rayDistance / float(steps);
    float jitter = sampleCloudNoise(vTexCoord * 23.0 + uTime * 0.01);

    vec3 sunDir = normalize(uSunDirection);
    vec3 moonDir = normalize(uMoonDirection);
    float day = clamp(uSkyIntensity, 0.0, 1.0);
    float ldotv = dot(ray, sunDir);
    float phase = mieCloudPhase(ldotv);
    float moonPhase = mieCloudPhase(dot(ray, moonDir));
    float sunVisibility = smoothstep(-0.06, 0.18, sunDir.y) * day;
    float moonVisibility = clamp(uMoonVisibility, 0.0, 1.0) * (1.0 - day);

    float eyeAltitude = max(uCameraPos.y, 0.0) + 100.0;
    vec3 atmos = sampleAtmosphere(ray, sunDir, moonDir, eyeAltitude, day, moonVisibility);
    vec3 sky = texture(uSkyCaptureTex, atmDirectionToSkyCaptureUv(ray)).rgb;
    vec3 horizon = mix(sky, uHorizonScatterColor, clamp(uHorizonScatterStrength, 0.0, 2.0) * 0.22);
    vec3 cloudColor = vec3(0.0);
    float transmittance = 1.0;

    for (int i = 0; i < 7; ++i) {
        if (i >= steps) {
            break;
        }
        float t = startT + (float(i) + jitter) * stepLength;
        vec3 pos = uCameraPos + ray * t;
        float height01 = clamp((pos.y - cloudBottom) / cloudThickness, 0.0, 1.0);
        vec2 cloudUv = pos.xz * 0.00135;
        float density = cloudDensityAt(cloudUv, height01, weatherCoverage) * max(uCloudDensity, 0.0);
        if (density <= 0.001) {
            continue;
        }

        vec2 sunProbeUv = (pos.xz + sunDir.xz * (62.0 + 130.0 * height01)) * 0.00135;
        float sunOcclusion = cloudDensityAt(sunProbeUv, clamp(height01 + sunDir.y * 0.22, 0.0, 1.0), weatherCoverage);
        float sunlight = exp(-sunOcclusion * mix(3.4, 8.5, clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0)));
        float powder = (1.0 - exp(-density * 16.0)) * mix(0.72, 1.15, clamp(1.0 - ldotv, 0.0, 1.0));
        float stepOpacity = 1.0 - exp(-density * stepLength * 0.012);
        stepOpacity = clamp(stepOpacity, 0.0, 0.42);

        vec3 sunlightColor = uSunLightColor * phase * sunVisibility * sunlight * (34.0 + powder * 32.0);
        vec3 moonlightColor = uMoonLightColor * moonPhase * moonVisibility * (7.0 + powder * 8.0);
        vec3 skylightColor = mix(uSkyAmbientColor, horizon + atmos * 0.16, 0.52) * (0.34 + 0.26 * height01);
        vec3 sampleColor = (sunlightColor + moonlightColor + skylightColor) * (0.46 + powder);
        sampleColor = mix(sampleColor, sampleColor * vec3(0.68, 0.75, 0.86), clamp(uWeatherWetness + uWeatherStorm, 0.0, 1.0) * 0.55);
        cloudColor += transmittance * sampleColor * stepOpacity;
        transmittance *= 1.0 - stepOpacity;
    }

    float opacity = clamp(1.0 - transmittance, 0.0, 1.0);
    float distanceFade = exp(-startT * (0.00020 + 0.00018 * clamp(uWeatherWetness, 0.0, 1.0)));
    opacity *= distanceFade * smoothstep(-0.02, 0.12, ray.y);
    cloudColor += atmos * opacity * mix(0.32, 0.58, clamp(uHorizonScatterStrength, 0.0, 1.0));
    cloudColor = mix(cloudColor, horizon * opacity, clamp(uWeatherStorm, 0.0, 1.0) * 0.18);

    FragColor = vec4(max(cloudColor, vec3(0.0)), opacity);
}
