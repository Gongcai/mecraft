#version 330 core
out vec4 FragColor;

in vec2 vUV;
in float vShade;

uniform sampler2D uAtlas;

void main() {
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    FragColor = vec4(texColor.rgb * vShade, texColor.a);
}

