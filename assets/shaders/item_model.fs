#version 450 core
out vec4 FragColor;

in vec2 vUV;
in float vShade;

uniform sampler2D uAtlas;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    vec4 texColor = texture(uAtlas, vUV);
    if (texColor.a < 0.1) {
        discard;
    }

    FragColor = vec4(srgbToLinear(texColor.rgb) * vShade, texColor.a);
}

