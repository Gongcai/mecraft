#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;
uniform sampler2D uNoiseTex;

uniform bool uBloomEnabled;
uniform float uBloomStrength;
uniform bool uSunRaysEnabled;
uniform vec2 uSunScreenPos;
uniform float uSunVisibility;
uniform float uSunRayStrength;
uniform bool uShaderpackGradingEnabled;
uniform int uTonemapMode;
uniform float uColorTemperature;
uniform float uVibrance;
uniform float uNoiseDitherStrength;
uniform bool uUnderwaterEnabled;
uniform vec3 uUnderwaterTint;
uniform float uUnderwaterStrength;
uniform float uScreenRollRadians;
uniform float uExposure;
uniform float uGamma;
uniform float uSaturation;
uniform float uContrast;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 tonemapReinhard(vec3 color) {
    return color / (color + vec3(1.0));
}

vec3 tonemapAces(vec3 color) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 tonemapFilmic(vec3 color) {
    color = max(vec3(0.0), color - vec3(0.004));
    return clamp((color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06), 0.0, 1.0);
}

float tonemapReinhardScalar(float value) {
    return value / (value + 1.0);
}

float tonemapAcesScalar(float value) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

float tonemapFilmicScalar(float value) {
    value = max(0.0, value - 0.004);
    return clamp((value * (6.2 * value + 0.5)) / (value * (6.2 * value + 1.7) + 0.06), 0.0, 1.0);
}

vec3 tonemapPreserveLuma(vec3 color) {
    color = max(color, vec3(0.0));
    float lumaIn = max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 0.00001);
    float lumaOut;
    if (uTonemapMode == 1) {
        lumaOut = tonemapAcesScalar(lumaIn);
    } else if (uTonemapMode == 2) {
        lumaOut = tonemapFilmicScalar(lumaIn);
    } else {
        lumaOut = tonemapReinhardScalar(lumaIn);
    }

    vec3 lumaMapped = color * (lumaOut / lumaIn);
    vec3 channelMapped;
    if (uTonemapMode == 1) {
        channelMapped = tonemapAces(color);
    } else if (uTonemapMode == 2) {
        channelMapped = tonemapFilmic(color);
    } else {
        channelMapped = tonemapReinhard(color);
    }
    float highlight = smoothstep(0.35, 2.5, lumaIn);
    return clamp(mix(channelMapped, lumaMapped, 0.72 + 0.18 * highlight), 0.0, 1.0);
}

vec3 applyColorTemperature(vec3 color) {
    float t = clamp(uColorTemperature, 0.0, 2.0) - 1.0;
    vec3 warm = vec3(1.08, 1.00, 0.90);
    vec3 cool = vec3(0.90, 0.98, 1.10);
    return color * mix(vec3(1.0), t >= 0.0 ? warm : cool, abs(t));
}

vec3 applyVibrance(vec3 color) {
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float maxChannel = max(max(color.r, color.g), color.b);
    float minChannel = min(min(color.r, color.g), color.b);
    float colorfulness = clamp(maxChannel - minChannel, 0.0, 1.0);
    float amount = uVibrance * (1.0 - colorfulness);
    return mix(vec3(luminance), color, 1.0 + amount);
}

vec3 applyGrade(vec3 color) {
    color *= max(uExposure, 0.001);
    if (uShaderpackGradingEnabled) {
        color = applyColorTemperature(color);
        color = tonemapPreserveLuma(color);
        color = applyVibrance(color);
    } else {
        color = vec3(1.0) - exp(-color);
    }
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, uSaturation);
    color = (color - 0.5) * uContrast + 0.5;
    return pow(max(color, vec3(0.0)), vec3(1.0 / max(uGamma, 0.001)));
}

void main() {
    vec2 centeredUv = vTexCoord - vec2(0.5, 0.5);
    float c = cos(uScreenRollRadians);
    float s = sin(uScreenRollRadians);
    mat2 rot = mat2(c, -s,
                    s,  c);
    vec2 rolledUv = rot * centeredUv + vec2(0.5, 0.5);

    vec3 color = texture(uSceneTex, rolledUv).rgb;
    if (uBloomEnabled) {
        vec3 bloom = texture(uBloomTex, rolledUv).rgb;
        color += bloom * uBloomStrength;

        if (uSunRaysEnabled && uSunVisibility > 0.001 && uSunRayStrength > 0.001) {
            vec2 toSun = uSunScreenPos - rolledUv;
            float screenFade = 1.0 - smoothstep(0.55, 1.15, length(uSunScreenPos - vec2(0.5)));
            float rayMask = clamp(uSunVisibility * screenFade, 0.0, 1.0);
            vec3 rays = vec3(0.0);
            float weight = 0.16;
            for (int i = 1; i <= 8; ++i) {
                float t = float(i) / 8.0;
                vec2 sampleUv = rolledUv + toSun * t * 0.86;
                vec2 inBounds = step(vec2(0.0), sampleUv) * step(sampleUv, vec2(1.0));
                float valid = inBounds.x * inBounds.y;
                rays += texture(uBloomTex, sampleUv).rgb * weight * valid;
                weight *= 0.82;
            }
            color += rays * uSunRayStrength * rayMask;
        }
    }

    if (uUnderwaterEnabled) {
        float strength = clamp(uUnderwaterStrength, 0.0, 1.0);
        vec3 underwaterTint = srgbToLinear(uUnderwaterTint);
        vec3 tinted = color * underwaterTint;
        color = mix(color, tinted, strength);

        float fog = clamp((1.0 - vTexCoord.y) * 0.10 * strength, 0.0, 0.12);
        color = mix(color, underwaterTint, fog);
    }

    vec3 graded = applyGrade(color);
    if (uNoiseDitherStrength > 0.0) {
        float noise = texture(uNoiseTex, gl_FragCoord.xy / vec2(textureSize(uNoiseTex, 0))).r - 0.5;
        graded += noise * uNoiseDitherStrength;
    }
    FragColor = vec4(clamp(graded, 0.0, 1.0), 1.0);
}

