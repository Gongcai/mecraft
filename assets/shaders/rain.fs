#version 450 core
out vec4 FragColor;

in float vAlpha;

uniform float uRainStrength;

void main() {
    // Rain streak: slightly blue-tinted white, modulated by per-vertex alpha.
    vec3 rainColor = vec3(0.72, 0.78, 0.85);
    FragColor = vec4(rainColor, vAlpha);
}
