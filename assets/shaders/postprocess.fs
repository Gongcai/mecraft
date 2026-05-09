#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;

uniform bool uBloomEnabled;
uniform float uBloomStrength;
uniform bool uUnderwaterEnabled;
uniform vec3 uUnderwaterTint;
uniform float uUnderwaterStrength;
uniform float uScreenRollRadians;
uniform float uExposure;
uniform float uGamma;
uniform float uSaturation;
uniform float uContrast;

vec3 applyGrade(vec3 color) {
    color = vec3(1.0) - exp(-color * max(uExposure, 0.001));
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
        color += texture(uBloomTex, rolledUv).rgb * uBloomStrength;
    }

    if (uUnderwaterEnabled) {
        float strength = clamp(uUnderwaterStrength, 0.0, 1.0);
        vec3 tinted = color * uUnderwaterTint;
        color = mix(color, tinted, strength);

        float fog = clamp((1.0 - vTexCoord.y) * 0.15 * strength, 0.0, 0.2);
        color = mix(color, uUnderwaterTint, fog);
    }

    FragColor = vec4(applyGrade(color), 1.0);
}

