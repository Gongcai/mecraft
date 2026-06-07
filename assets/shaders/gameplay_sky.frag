#version 450 core
in vec3 vWorldDir;
in vec2 vUV;
in vec4 vColor;

out vec4 FragColor;

uniform int uMode;
uniform sampler2D uTexture;
uniform vec3 uSkyTopColor;
uniform vec3 uSkyHorizonColor;
uniform vec3 uSunDirection;
uniform vec3 uMoonDirection;
uniform vec3 uSunScatterColor;
uniform vec3 uMoonLightColor;
uniform vec4 uTintColor;
uniform float uHorizonHaze;
uniform float uSunGlare;
uniform float uSunVisibility;
uniform float uMoonVisibility;
uniform float uMoonPhaseAngle;
uniform float uNightFactor;
uniform float uBlackKeyThreshold;
uniform float uBlackKeySoftness;
uniform int uIncludeCelestialDisks;
uniform int uCloudySkyCapture;
uniform sampler2D uNoiseTex;
uniform bool uNoiseEnabled;
uniform float uTime;
uniform float uCloudTimeScale;
uniform float uCloudCoverage;
uniform float uCloudDensity;
uniform float uCloudHeight;
uniform float uCloudThickness;
uniform float uPlanarCloudCoverage;
uniform float uPlanarCloudDensity;
uniform float uPlanarCloudAltitude;
uniform vec3 uCameraPos;

// CPU illuminance uniforms — legacy/fallback only. Mode 5 computes illuminance
// from atmosphere LUT via atmGetSunAndSkyIrradiance(), not from these uniforms.
uniform vec3 uDirectIlluminance;
uniform vec3 uSkyIlluminance;
uniform vec3 uSunIlluminance;
uniform vec3 uMoonIlluminance;
uniform vec3 uCloudDynamicWeather;

// Weather modulation for SkyCapture radiance (mode 4).
// Metadata mode 5 intentionally stays aligned with DerivativeMain
// GetSunAndSkyIrradiance(), which is weather-independent.
uniform float uWeatherWetness;
uniform float uWeatherStorm;
uniform float uSkyWetness;
uniform float uSurfaceWetness;
uniform float uFogWetness;
uniform float uCloudWetness;
uniform float uPrecipitation;

// Atmosphere LUT (modes 4, 5)
uniform sampler3D uAtmosphereLut;
uniform float uCameraAltitude;

// Sky capture texture for mode 0 visible sky (atmosphere LUT radiance)
uniform sampler2D uSkyCaptureTex;
uniform int uSkyCaptureEnabled;

#include "atmosphere_lut.glsl"
#include "render_contract.glsl"
#include "cloud_density.glsl"

const float kPi = 3.14159265359;
const float kTwoPi = 6.28318530718;

// DerivativeMain Settings.glsl: STARS_COVERAGE=0.15, STARS_INTENSITY=0.1
const float STARS_COVERAGE = 0.15;
const float STARS_INTENSITY = 0.1;

// Approximate blackbody radiation color for temperature range 4000K-8000K.
// DerivativeMain uses a full Planck function; this polynomial approximation
// captures the warm-orange to cool-blue-white transition visible in stars.
vec3 Blackbody(float t) {
    // t in [0,1]: 0=4000K (warm), 1=8000K (cool)
    float r = 1.0;
    float g = 0.56 + 0.22 * t;
    float b = 0.24 + 0.60 * t;
    return vec3(r, g, b);
}

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    float x = hash12(p + vec2(17.13, 3.71));
    float y = hash12(p + vec2(5.29, 41.37));
    return vec2(x, y);
}

// DerivativeMain Atmosphere.glsl:809-835 RenderStars()
// 3D grid with sun vector rotation and blackbody color temperature.
vec3 renderStars(vec3 worldDir, vec3 sunDir) {
    const float scale = 256.0;
    const float coverage = 0.1 * STARS_COVERAGE;
    const float maxLuminance = 0.6 * STARS_INTENSITY;
    const float minTemperature = 4000.0;
    const float maxTemperature = 8000.0;

    // Rodrigues' rotation: align star field with sun direction
    // DerivativeMain Atmosphere.glsl:818-821
    float cosine = sunDir.z;
    vec3 axis = cross(sunDir, vec3(0.0, 0.0, 1.0));
    float cosecantSquared = 1.0 / max(dot(axis, axis), 1e-10);
    worldDir = cosine * worldDir + cross(axis, worldDir)
             + cosecantSquared * (1.0 - cosine) * dot(axis, worldDir) * axis;

    // 3D grid hashing
    vec3 p = worldDir * scale;
    ivec3 i = ivec3(floor(p));
    vec3 f = p - vec3(i);
    float r = dot(f - 0.5, f - 0.5);

    vec3 i3 = fract(vec3(i) * vec3(443.897, 441.423, 437.195));
    i3 += dot(i3, i3.yzx + 19.19);
    vec2 hash = fract((i3.xx + i3.yz) * i3.zy);
    hash.y = 2.0 * hash.y - 4.0 * hash.y * hash.y + 3.0 * hash.y * hash.y * hash.y;

    // Coverage gating: remap(hash.x) from [1-coverage, 1] to [0, 1]
    float cov = clamp((hash.x - (1.0 - coverage)) / coverage, 0.0, 1.0);
    // Distance falloff from cell center
    float falloff = clamp((0.25 - r) / 0.25, 0.0, 1.0);

    return maxLuminance * falloff * cov * cov * Blackbody(mix(0.0, 1.0, hash.y));
}

#include "procedural_celestials.glsl"

vec3 evaluateSkyRadiance(vec3 dir) {
    float height = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    float gradient = smoothstep(0.0, 1.0, height);
    vec3 color = mix(uSkyHorizonColor, uSkyTopColor, gradient);

    float horizon = pow(1.0 - clamp(abs(dir.y), 0.0, 1.0), 2.25);
    color = mix(color, uSkyHorizonColor * 1.12, horizon * clamp(uHorizonHaze, 0.0, 1.0));

    float sunDot = max(dot(dir, normalize(uSunDirection)), 0.0);
    float glow = pow(sunDot, 24.0) * uSunGlare;
    float wideGlow = pow(sunDot, 4.0) * uSunGlare * 0.22;
    color += uSunScatterColor * (glow + wideGlow) * smoothstep(-0.08, 0.18, uSunDirection.y);

    float moonDot = max(dot(dir, normalize(uMoonDirection)), 0.0);
    float moonGlow = pow(moonDot, 36.0) * 0.32 + pow(moonDot, 8.0) * 0.070;
    color += uMoonLightColor * moonGlow * clamp(uMoonVisibility, 0.0, 1.0);

    float nightHorizon = horizon * clamp(uNightFactor, 0.0, 1.0);
    color += vec3(0.04, 0.08, 0.12) * nightHorizon;
    vec3 stars = renderStars(dir, normalize(uSunDirection))
               * clamp(uNightFactor, 0.0, 1.0)
               * (1.0 - clamp(uSunVisibility, 0.0, 1.0));
    color += stars;
    return color;
}

float captureNoiseDetail(vec3 worldDir) {
    vec3 dir = worldDir * 48.0;
    vec3 wind = vec3(2e-3, 2e-4, 1e-3) * (uTime * uCloudTimeScale);
    float pnoise = cloudNoiseSharp(dir - wind);       dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 2.0);             dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 4.0) * 0.5;       dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 8.0) * 0.25;      dir += pnoise * 1e-3 - wind;
    pnoise += cloudNoiseSharp(dir * 16.0) * 0.125;    dir += pnoise * 1e-3 - wind;
    return pnoise - 0.15;
}

vec4 capturePlanarClouds(vec3 worldDir, float LdotV, vec3 skyRadiance, vec3 sunDir, vec3 moonDir) {
    if (worldDir.y <= 0.01) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float altitude = max(uPlanarCloudAltitude, 64.0);
    float distanceToPlane = altitude / max(worldDir.y, 0.01);
    vec2 cloudXZ = worldDir.xz * distanceToPlane;
    float cirrus = cirrusCloudDensity(cloudXZ, clamp(uPlanarCloudCoverage, 0.0, 1.0));
    float cirrocumulus = cirrocumulusDensity(cloudXZ);
    float coverage = clamp((cirrus + cirrocumulus) * clamp(uPlanarCloudDensity, 0.0, 2.0), 0.0, 1.0);
    if (coverage <= 1e-4) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float wetness = clamp(uCloudWetness, 0.0, 1.0);
    float forward = atmHenyeyGreensteinPhase(LdotV, 0.6 - wetness * 0.2) * 0.7;
    float backward = atmHenyeyGreensteinPhase(LdotV, -0.4 + wetness * 0.2) * 0.25;
    float peak = cloudCornetteShanksPhase(LdotV, 0.9) * (0.1 + 0.7 * wetness);
    float phase = forward + backward + peak;

    vec3 sunLight = uSunIlluminance * clamp(uSunVisibility, 0.0, 1.0);
    vec3 moonLight = uMoonIlluminance * clamp(uMoonVisibility, 0.0, 1.0);
    vec3 light = max(sunLight + moonLight, vec3(0.0));
    vec3 cloudLit = skyRadiance * (0.25 + 0.55 * wetness);
    cloudLit += light * phase * mix(4.0, 1.4, wetness);
    cloudLit += uSkyIlluminance * mix(0.35, 0.18, wetness);

    float atmosFade = exp(-distanceToPlane * (0.1 + 0.1 * wetness) * 0.00015);
    vec3 color = mix(skyRadiance * coverage, cloudLit * coverage, atmosFade);
    float transmittance = exp(-coverage * mix(1.15, 2.4, wetness));
    return vec4(max(color, vec3(0.0)), transmittance);
}

vec4 captureVolumetricClouds(vec3 worldDir, float LdotV, vec3 skyRadiance, vec3 sunDir, vec3 moonDir) {
    // DerivativeMain VolumetricClouds.glsl:57-61: storm intensity raises cloud altitude
    float stormZ = uCloudDynamicWeather.z;
    float cloudBottom = max(uCloudHeight * (1.0 + stormZ * 2.0), 64.0);
    float cloudTop = cloudBottom + max(uCloudThickness, 16.0);
    if (worldDir.y <= 0.01) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float startT = cloudBottom / max(worldDir.y, 0.01);
    float endT = cloudTop / max(worldDir.y, 0.01);
    if (endT <= startT || startT < 0.0) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    const int steps = 10;
    float stepLen = (endT - startT) / float(steps);
    vec3 rayStep = worldDir * stepLen;
    vec3 rayPos = worldDir * (startT + stepLen * 0.5);
    float noiseDetail = captureNoiseDetail(worldDir);
    float transmittance = 1.0;

    for (int i = 0; i < steps; ++i, rayPos += rayStep) {
        float h = clamp((rayPos.y - cloudBottom) / max(cloudTop - cloudBottom, 1.0), 0.0, 1.0);
        float density = cloudDensityAt(rayPos, h, clamp(uCloudCoverage * (1.0 - stormZ * 0.3), 0.05, 1.0), noiseDetail);
        if (density <= 1e-4) {
            continue;
        }
        transmittance *= exp(-density * 0.12 * stepLen);
        if (transmittance < 0.03) {
            break;
        }
    }

    float opacity = clamp(1.0 - transmittance, 0.0, 1.0);
    if (opacity <= 1e-4) {
        return vec4(0.0, 0.0, 0.0, 1.0);
    }

    float wetness = clamp(uCloudWetness, 0.0, 1.0);
    float phase = atmHenyeyGreensteinPhase(LdotV, 0.6 - wetness * 0.2) * 0.7
                + atmHenyeyGreensteinPhase(LdotV, -0.4 + wetness * 0.2) * 0.25
                + cloudCornetteShanksPhase(LdotV, 0.9) * (0.1 + 0.7 * wetness);
    vec3 sunLight = uSunIlluminance * clamp(uSunVisibility, 0.0, 1.0);
    vec3 moonLight = uMoonIlluminance * clamp(uMoonVisibility, 0.0, 1.0);
    // DerivativeMain VolumetricClouds.glsl:66-67: storm boosts lighting slightly
    vec3 cloudLit = (sunLight + moonLight) * phase * mix(8.0, 2.0, wetness) * (1.0 + stormZ * 0.2);
    cloudLit += uSkyIlluminance * mix(0.28, 0.12, wetness) * (1.0 + stormZ * 0.2);

    float meanDistance = mix(startT, endT, 0.5);
    float atmosFade = exp(-meanDistance * (0.2 + 0.1 * wetness) * 1e-4);
    vec3 color = cloudLit * opacity * atmosFade + skyRadiance * opacity * (1.0 - atmosFade);
    return vec4(max(color, vec3(0.0)), transmittance);
}

vec3 captureCloudySkybox(vec3 worldDir, vec3 skyRadiance, vec3 sunDir, vec3 moonDir, vec3 transmittance) {
    float LdotV = dot(worldDir, sunDir);
    vec4 cloudsData = vec4(0.0, 0.0, 0.0, 1.0);

    vec4 volumeClouds = captureVolumetricClouds(worldDir, LdotV, skyRadiance, sunDir, moonDir);
    cloudsData.rgb += volumeClouds.rgb * cloudsData.a;
    cloudsData.a *= volumeClouds.a;

    vec4 planarClouds = capturePlanarClouds(worldDir, LdotV, skyRadiance, sunDir, moonDir);
    cloudsData.rgb += planarClouds.rgb * cloudsData.a;
    cloudsData.a *= planarClouds.a;

    vec3 skyboxData = skyRadiance * cloudsData.a + cloudsData.rgb;
    return max(skyboxData, vec3(0.0));
}

void main() {
    if (uMode == 0) {
        // Visible sky: prefer SkyCapture (atmosphere LUT) for consistency with deferred.
        // Fallback to gradient model if SkyCapture texture is not bound (forward path).
        vec3 dir = normalize(vWorldDir);
        vec3 sky;
        if (uSkyCaptureEnabled != 0) {
            sky = sampleSkyRadiance(uSkyCaptureTex, dir);
            float solarLobeMask = proceduralSunAngularMask(dir, 0.018, 0.180);
            float solarCoreMask = proceduralSunAngularMask(dir, 0.000, 0.070);
            sky *= (1.0 - solarLobeMask * 0.94) * (1.0 - solarCoreMask * 0.92);
        } else {
            // Forward fallback: evaluateSkyRadiance() is already in display-referred space,
            // no additional srgbToLinear needed (was applied in the old path).
            sky = evaluateSkyRadiance(dir);
        }
        // DerivativeMain Atmosphere.glsl:809-835: 3D blackbody stars with sun rotation
        vec3 stars = renderStars(dir, normalize(uSunDirection))
                   * clamp(uNightFactor, 0.0, 1.0)
                   * (1.0 - clamp(uSunVisibility, 0.0, 1.0));
        sky += stars;
        sky += renderProceduralMoonDisk(dir);
        sky += renderProceduralSunDisk(dir);
        FragColor = vec4(max(sky, vec3(0.0)), 1.0);
        return;
    }

    if (uMode == 4) {
        // DerivativeMain-compatible sky capture projection.
        // Matches UnprojectSky in DerivativeMain Atmosphere.glsl.
        vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
        float u = fract((uv.x - 2.0 / float(skyCaptureRes.x)) / (1.0 - 4.0 / float(skyCaptureRes.x)));
        float phi = u * kTwoPi;
        float theta = uv.y * kPi;
        float sinTheta = sin(theta);
        vec3 dir = normalize(vec3(sin(phi) * sinTheta, cos(theta), cos(phi) * sinTheta));

        vec3 sunDir = normalize(uSunDirection);
        vec3 moonDir = normalize(uMoonDirection);

        vec3 transmittance;
        vec3 sky = atmGetSkyRadiance(max(uCameraAltitude, 0.0), dir, sunDir, transmittance);
        if (uCloudySkyCapture != 0) {
            sky = captureCloudySkybox(dir, sky, sunDir, moonDir, transmittance);
        } else if (uIncludeCelestialDisks != 0) {
            // Visible disks are procedural in the final sky passes; SkyCapture keeps only radiance.
        }

        // DerivativeMain/lib/Atmosphere/Atmosphere.glsl:
        // rayleigh = mix(rayleigh, GetLuminance(rayleigh) * wetnessGrey, wetness * 0.7);
        // return (rayleigh + mie) * oneMinus(wetness * 0.6);
        // Mecraft adaptation: atmosphere LUT returns combined sky radiance here,
        // so apply the same shaping to combined radiance.
        float weatherOcclusion = clamp(uSkyWetness, 0.0, 1.0);
        if (weatherOcclusion > 0.001) {
            float skyLum = dot(sky, vec3(0.2126, 0.7152, 0.0722));
            vec3 wetnessGrey = skyLum * vec3(1.026186824, 0.9881671071, 1.015787125);
            sky = mix(sky, wetnessGrey, weatherOcclusion * 0.7);
            sky *= 1.0 - weatherOcclusion * 0.6;
        }

        FragColor = vec4(max(sky, vec3(0.0)), 1.0);
        return;
    }

    if (uMode == 5) {
        // Sky cache metadata texel pass — illuminance computed from atmosphere LUT.
        // Rendered as a 1x6 viewport at column x=255 of the sky capture FBO.
        int row = int(gl_FragCoord.y);
        vec3 camera = vec3(0.0, atmPlanetRadius + max(uCameraAltitude, 0.0), 0.0);
        vec3 sunDir = normalize(uSunDirection);

        vec3 sunIrr, moonIrr;
        vec3 skyIrr = atmGetSunAndSkyIrradiance(camera, sunDir, sunIrr, moonIrr);

        vec3 value = vec3(0.0);
        // DerivativeMain GetSunAndSkyIrradiance() does not apply wetness here.
        // Rain/overcast direct-light attenuation happens later through cloudShadow
        // in deferred lighting, while sky radiance wetness is applied in mode 4.
        if (row == 0) value = sunIrr + moonIrr;   // directIlluminance
        else if (row == 1) value = skyIrr;         // skyIlluminance
        else if (row == 2) value = sunIrr;         // sunIlluminance
        else if (row == 3) value = moonIrr;        // moonIlluminance
        else if (row == 5) value = uCloudDynamicWeather; // cloudDynamicWeather
        else discard;
        FragColor = vec4(value, 1.0);
        return;
    }

    if (uMode == 1) {
        vec4 texel = texture(uTexture, vUV);
        float brightness = max(max(texel.r, texel.g), texel.b);
        float keyedAlpha = smoothstep(uBlackKeyThreshold, uBlackKeyThreshold + uBlackKeySoftness, brightness);
        if (keyedAlpha <= 0.001) {
            discard;
        }
        vec3 unassociatedColor = texel.rgb / max(keyedAlpha, 0.001);
        unassociatedColor = min(unassociatedColor, vec3(1.0));
        vec3 color = srgbToLinear(unassociatedColor) * srgbToLinear(uTintColor.rgb);
        FragColor = vec4(color, texel.a * keyedAlpha * uTintColor.a);
        return;
    }

    if (uMode == 3) {
        FragColor = vec4(srgbToLinear(uTintColor.rgb) * vColor.r, uTintColor.a);
        return;
    }

    FragColor = vec4(srgbToLinear(vColor.rgb) * srgbToLinear(uTintColor.rgb), vColor.a * uTintColor.a);
}
