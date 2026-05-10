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

const float kPi = 3.14159265359;
const float kTwoPi = 6.28318530718;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
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
    float moonGlow = pow(moonDot, 36.0) * 0.24 + pow(moonDot, 8.0) * 0.045;
    color += uMoonLightColor * moonGlow * clamp(uMoonVisibility, 0.0, 1.0);

    float nightHorizon = horizon * clamp(uNightFactor, 0.0, 1.0);
    color += vec3(0.04, 0.08, 0.12) * nightHorizon;
    return color;
}

void main() {
    if (uMode == 0) {
        vec3 dir = normalize(vWorldDir);
        FragColor = vec4(srgbToLinear(evaluateSkyRadiance(dir)), 1.0);
        return;
    }

    if (uMode == 4) {
        vec2 uv = clamp(vUV, vec2(0.0), vec2(1.0));
        float phi = uv.x * kTwoPi - kPi;
        float cosTheta = uv.y * 2.0 - 1.0;
        float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
        vec3 dir = normalize(vec3(sin(phi) * sinTheta, cosTheta, -cos(phi) * sinTheta));
        FragColor = vec4(srgbToLinear(evaluateSkyRadiance(dir)), 1.0);
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
