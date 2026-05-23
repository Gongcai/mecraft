#version 450 core
out vec4 FragColor;

in vec2 vUV;

uniform sampler2D uPrecipTex;
uniform float uPrecipStrength;
uniform float uPrecipAlphaScale;
uniform vec3 uPrecipColor;
uniform int uMaskPass;

void main() {
    float texAlpha = texture(uPrecipTex, vUV).a * uPrecipAlphaScale;
    if (uMaskPass != 0) {
        float verticalFade = smoothstep(0.02, 0.20, vUV.y) * (1.0 - smoothstep(0.80, 0.98, vUV.y));
        texAlpha = verticalFade * min(uPrecipAlphaScale, 1.0);
    }
    FragColor = vec4(uPrecipColor, texAlpha * uPrecipStrength);
}
