#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 viewProj;
uniform float uWindTime;
uniform float uWindStrength;
uniform float uWindSpeed;
uniform float uWindSpatialFreq;

out vec2 vUV;
out float vNormal;
out float vFogDist;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);

    // Negative marker values are reserved for cross vegetation (grass/flowers).
    if (aNormal < -0.5) {
        float phase = worldPos.x * uWindSpatialFreq + worldPos.z * (uWindSpatialFreq * 1.37) + uWindTime * uWindSpeed;
        float swayX = sin(phase);
        float swayZ = cos(phase * 0.91 + 1.7);
        worldPos.xz += vec2(swayX, swayZ) * uWindStrength;
    }

    vec4 viewPos = view * worldPos;
    gl_Position = viewProj * worldPos;

    vUV = aUV;
    vNormal = aNormal;
    vFogDist = max(0.0, -viewPos.z);
}

