#version 450 core
out vec4 FragColor;

in vec2 vUV;

uniform sampler2D uPrecipTex;
uniform float uPrecipStrength;
uniform vec3 uPrecipColor;

void main() {
    float texAlpha = texture(uPrecipTex, vUV).r;
    FragColor = vec4(uPrecipColor, texAlpha * uPrecipStrength);
}
