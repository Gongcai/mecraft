#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;

uniform mat4 viewProj;
uniform mat4 model;

out vec2 vUV;
out vec3 vWorldPos;
out vec3 vLocalPos;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    vUV = aUV;
    vWorldPos = worldPos.xyz;
    vLocalPos = aPos;
    gl_Position = viewProj * worldPos;
}

