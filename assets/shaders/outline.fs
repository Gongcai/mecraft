#version 450 core
out vec4 FragColor;

uniform vec3 lineColor;

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

void main() {
    FragColor = vec4(srgbToLinear(lineColor), 1.0);
}

