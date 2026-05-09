#version 450 core

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSceneTex;
uniform float uThreshold;

void main() {
    vec3 color = texture(uSceneTex, vTexCoord).rgb;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float mask = smoothstep(uThreshold, uThreshold + 0.35, luma);
    FragColor = vec4(color * mask, 1.0);
}
