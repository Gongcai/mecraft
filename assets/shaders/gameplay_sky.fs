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
uniform float uNightFactor;
uniform float uBlackKeyThreshold;
uniform float uBlackKeySoftness;
uniform int uIncludeCelestialDisks;

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

#include "atmosphere_lut.glsl"
#include "render_contract.glsl"

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

void main() {
    if (uMode == 0) {
        // Visible sky: prefer SkyCapture (atmosphere LUT) for consistency with deferred.
        // Fallback to gradient model if SkyCapture texture is not bound (forward path).
        vec3 dir = normalize(vWorldDir);
        vec3 sky;
        if (textureSize(uSkyCaptureTex, 0).x > 0) {
            sky = sampleSkyRadiance(uSkyCaptureTex, dir);
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
        if (uIncludeCelestialDisks != 0) {
            sky += atmRenderSun(dir, sunDir) * transmittance * clamp(uSunVisibility, 0.0, 1.0);
            sky += atmRenderMoon(dir, moonDir) * transmittance * clamp(uMoonVisibility, 0.0, 1.0) * max(uMoonPhaseFlux, 0.0);
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
