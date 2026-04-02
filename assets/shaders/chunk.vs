#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;

uniform mat4 model;
uniform mat4 viewProj;

out vec2 vUV;
out float vNormal;

void main() {
    gl_Position = viewProj * model * vec4(aPos, 1.0);

    vUV = aUV;
    vNormal = aNormal;
}

