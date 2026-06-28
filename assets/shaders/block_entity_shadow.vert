// Instanced block entity shadow vertex shader.
// Each instance supplies its model matrix and skylight.

#version 450 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in mat4 iModel;
layout(location = 7) in vec2 iLight;

uniform mat4 viewProj;

out vec2 vUV;
out vec3 vWorldPos;
out vec3 vNormal;
out float vEntitySunlight;

void main() {
    vec4 worldPos = iModel * vec4(aPos, 1.0);
    gl_Position = viewProj * worldPos;
    vUV = aUV;
    vWorldPos = worldPos.xyz;
    vNormal = normalize(mat3(iModel) * aNormal);
    vEntitySunlight = iLight.x;
}
