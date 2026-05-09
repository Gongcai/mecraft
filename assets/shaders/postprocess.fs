#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform sampler2D uBloomTex;

uniform bool uBloomEnabled;
uniform float uBloomStrength;
uniform bool uSunRaysEnabled;
uniform vec2 uSunScreenPos;
uniform float uSunVisibility;
uniform float uSunRayStrength;
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

    FragColor = vec4(applyGrade(color), 1.0);
}

