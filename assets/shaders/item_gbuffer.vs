// Item drop GBuffer vertex shader — Mecraft Phase 5.3 extension.
// Renders dropped non-block items into the GBuffer.
// Vertex layout matches ItemModelVertex: pos(3f), uv(2f), shade(1f).
// Items have no per-vertex normals; a default upward-facing normal is assigned
// in the fragment shader.

#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aShade;

uniform mat4 viewProj;
uniform mat4 model;

out vec2 vUV;
out float vShade;
out vec3 vWorldPos;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = viewProj * worldPos;
    vUV = aUV;
    vShade = aShade;
    vWorldPos = worldPos.xyz;
}
