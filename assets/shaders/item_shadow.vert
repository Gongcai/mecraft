// Item drop shadow depth vertex shader — Mecraft Phase 5.3 extension.
// Renders dropped non-block items into the CSM shadow map.
// Vertex layout matches ItemModelVertex: pos(3f), uv(2f), shade(1f), normal(3f).

#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aShade;
layout (location = 3) in vec3 aNormal;

uniform mat4 viewProj;
uniform mat4 model;

out vec2 vUV;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = viewProj * worldPos;
    vUV = aUV;
}
