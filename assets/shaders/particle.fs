#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vAlpha;
in float vGrassTintFactor;

uniform sampler2D texAtlas;
uniform vec3 uGrassTintColor;

void main() {
    vec4 texColor = texture(texAtlas, vUV);
    if (texColor.a < 0.1)
        discard;
    if (vGrassTintFactor > 0.5) {
        texColor.rgb *= uGrassTintColor;
    }
    FragColor = vec4(texColor.rgb, texColor.a * vAlpha);
}
