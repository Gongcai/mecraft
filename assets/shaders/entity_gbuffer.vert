// Entity GBuffer vertex shader — Mecraft extension.
// Renders humanoid/mob entities into the same GBuffer as terrain
// so they receive deferred lighting, CSM shadows, SSAO, and volumetric fog.
// Vertex layout matches SteveVertex: pos(3f), uv(2f), normal(3f).

#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;

uniform mat4 model;
uniform mat4 prevModel;
uniform mat4 viewProj;
uniform mat4 prevViewProj;

out vec2 vUV;
out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vVelocity;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    gl_Position = viewProj * worldPos;
    vUV = aUV;
    vNormal = normalize(mat3(model) * aNormal);
    vWorldPos = worldPos.xyz;

    // Per-object velocity in UV space, matching the fullscreen velocity pass.
    vec4 prevClip = prevViewProj * prevModel * vec4(aPos, 1.0);
    vec2 curNdc = gl_Position.xy / max(gl_Position.w, 0.00001);
    vec2 prevNdc = prevClip.xy / max(prevClip.w, 0.00001);
    vVelocity = (curNdc - prevNdc) * 0.5;
}
