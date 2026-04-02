#version 330 core
out vec4 FragColor;

in vec2 vUV;

uniform sampler2D texAtlas;

void main() {
    vec4 texColor = texture(texAtlas, vUV);

    if(texColor.a < 0.1)
        discard;

    FragColor = texColor;
}

