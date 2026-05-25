// Item drop GBuffer vertex shader — Mecraft Phase 5.3 extension.
// Renders dropped non-block items into the GBuffer.
// Vertex layout matches ItemModelVertex: pos(3f), uv(2f), shade(1f), normal(3f).

#version 450 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aShade;
layout (location = 3) in vec3 aNormal;

uniform mat4 viewProj;
uniform mat4 model;
uniform mat4 prevModel;

out vec2 vUV;
out float vShade;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vVelocity;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = viewProj * worldPos;
    vUV = aUV;
    vShade = aShade;
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(model) * aNormal);

    // Per-object velocity for TAA/motion blur.
    vec4 prevClip = viewProj * prevModel * vec4(aPos, 1.0);
    vec2 curNdc = gl_Position.xy / max(gl_Position.w, 0.00001);
    vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.00001);
    vVelocity = curNdc - prevNdc;
}
