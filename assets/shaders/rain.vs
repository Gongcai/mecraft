#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in float aAlpha;

uniform mat4 viewProj;

out float vAlpha;

void main() {
    gl_Position = viewProj * vec4(aPos, 1.0);
    vAlpha = aAlpha;
}
