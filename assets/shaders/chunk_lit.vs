#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;     // face index or cross marker
layout (location = 3) in float aSunlight;   // 0.0 - 1.0  (pre-computed brightness)
layout (location = 4) in float aBlockLight; // 0.0 - 1.0  (pre-computed brightness)
layout (location = 5) in float aAO;         // 0.0 - 3.0
layout (location = 6) in float aLayer;

uniform mat4 model;
uniform mat4 view;
uniform mat4 viewProj;
uniform float uWindTime;
uniform float uWindStrength;
uniform float uWindSpeed;
uniform float uWindSpatialFreq;

out vec2 vUV;
out float vLight;
out float vSunlight;
out float vBlockLight;
out float vAO;
out float vNormal;
out float vLayer;
out float vFogDist;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);

    // Wind effect for cross vegetation (aNormal < -0.5 indicates cross quads)
    if (aNormal < -0.5) {
        float phase = worldPos.x * uWindSpatialFreq + worldPos.z * (uWindSpatialFreq * 1.37) + uWindTime * uWindSpeed;
        float swayX = sin(phase);
        float swayZ = cos(phase * 0.91 + 1.7);
        // Derive wind weight from vertex height: bottom vertices (y fraction near 0) don't sway,
        // top vertices (y fraction near 1) sway fully.
        float windWeight = fract(aPos.y);
        worldPos.xz += vec2(swayX, swayZ) * (uWindStrength * windWeight);
    }

    vec4 viewPos = view * worldPos;
    gl_Position = viewProj * worldPos;

    vUV = aUV;

    // Brightness values are pre-computed via the exponential decay curve in the CPU
    // mesher (averaged in brightness space for physically-correct smooth lighting).
    // Pass through as-is for GPU interpolation and fragment shader use.
    vSunlight = aSunlight;
    vBlockLight = aBlockLight;
    vLight = max(aSunlight, aBlockLight);

    vAO = aAO;
    vNormal = aNormal;
    vLayer = aLayer;
    vFogDist = max(0.0, -viewPos.z);
}
