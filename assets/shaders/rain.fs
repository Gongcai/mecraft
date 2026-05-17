#version 450 core
out vec4 FragColor;

in vec2 vUV;

uniform sampler2D uRainTex;
uniform float uRainStrength;

void main() {
    float texAlpha = texture(uRainTex, vUV).r;
    vec3 rainColor = vec3(0.72, 0.78, 0.85);
    FragColor = vec4(rainColor, texAlpha * uRainStrength);
}
