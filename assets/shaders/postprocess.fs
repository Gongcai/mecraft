#version 330 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;

uniform bool uUnderwaterEnabled;
uniform vec3 uUnderwaterTint;
uniform float uUnderwaterStrength;
uniform float uScreenRollRadians;

void main() {
    vec2 centeredUv = vTexCoord - vec2(0.5, 0.5);
    float c = cos(uScreenRollRadians);
    float s = sin(uScreenRollRadians);
    mat2 rot = mat2(c, -s,
                    s,  c);
    vec2 rolledUv = rot * centeredUv + vec2(0.5, 0.5);

    vec3 color = texture(uSceneTex, rolledUv).rgb;

    if (uUnderwaterEnabled) {
        float strength = clamp(uUnderwaterStrength, 0.0, 1.0);
        vec3 tinted = color * uUnderwaterTint;
        color = mix(color, tinted, strength);

        float fog = clamp((1.0 - vTexCoord.y) * 0.15 * strength, 0.0, 0.2);
        color = mix(color, uUnderwaterTint, fog);
    }

    FragColor = vec4(color, 1.0);
}

