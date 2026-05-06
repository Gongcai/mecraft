#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;     // face index or cross marker
layout (location = 3) in float aSunlight;   // 0.0 - 1.0  (raw level / 15)
layout (location = 4) in float aBlockLight; // 0.0 - 1.0  (raw level / 15)
layout (location = 5) in float aAO;         // 0.0 - 3.0
layout (location = 6) in float aLayer;
layout (location = 7) in float aAnimationFrameCount;
layout (location = 8) in float aAnimationFps;
layout (location = 9) in float aAnimated;

uniform mat4 view;
uniform mat4 viewProj;
uniform mat4 model;
uniform int uUseModel;

out vec2 vUV;
out float vLight;
out float vSunlight;
out float vBlockLight;
out float vAO;
out float vNormal;
out float vLayer;
out float vAnimationFrameCount;
out float vAnimationFps;
out float vAnimated;
out float vFogDist;

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    vec4 worldPos = (uUseModel != 0) ? model * localPos : localPos;
    vec4 viewPos = view * worldPos;
    gl_Position = viewProj * worldPos;

    vUV = aUV;

    // Raw light levels normalized to [0,1] are passed through directly.
    // The fragment shader will use them as lightmap UV coordinates.
    vSunlight = aSunlight;
    vBlockLight = aBlockLight;
    vLight = max(aSunlight, aBlockLight);

    vAO = aAO;
    vNormal = aNormal;
    vLayer = aLayer;
    vAnimationFrameCount = aAnimationFrameCount;
    vAnimationFps = aAnimationFps;
    vAnimated = aAnimated;
    vFogDist = max(0.0, -viewPos.z);
}
