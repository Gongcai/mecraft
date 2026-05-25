// Entity shadow depth vertex shader — Mecraft extension.
// Renders humanoid/mob entities into the CSM shadow map.
// Vertex layout matches SteveVertex: pos(3f), uv(2f), normal(3f).

#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 viewProj;

out vec2 vUV;
out vec3 vWorldPos;
out vec3 vNormal;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = viewProj * worldPos;
    vUV = aUV;
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(model) * aNormal);
}
