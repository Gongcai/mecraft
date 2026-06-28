#version 450 core
in vec2 vTexCoord;
out vec4 FragColor;
layout(binding = 0) uniform sampler2D uInputTex;

void main() {
    FragColor = texture(uInputTex, vTexCoord);
}
