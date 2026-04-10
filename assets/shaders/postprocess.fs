#version 330 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;

uniform bool uUnderwaterEnabled;
uniform vec3 uUnderwaterTint;
uniform float uUnderwaterStrength;

void main() {
    vec3 color = texture(uSceneTex, vTexCoord).rgb;

    if (uUnderwaterEnabled) {
        float strength = clamp(uUnderwaterStrength, 0.0, 1.0);
        vec3 tinted = color * uUnderwaterTint;
        color = mix(color, tinted, strength);

        float fog = clamp((1.0 - vTexCoord.y) * 0.15 * strength, 0.0, 0.2);
        color = mix(color, uUnderwaterTint, fog);
    }

    FragColor = vec4(color, 1.0);
}

