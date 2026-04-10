#version 330 core
out vec4 FragColor;

in vec2 vUV;

uniform sampler2D texAtlas;
uniform int uForceBaseLod;

void main() {
    vec4 texColor = (uForceBaseLod != 0)
        ? textureLod(texAtlas, vUV, 0.0)
        : texture(texAtlas, vUV);

    if (texColor.a < 0.1)
        discard;

    FragColor = texColor;
}
