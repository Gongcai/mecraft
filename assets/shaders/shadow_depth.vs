#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aUV;
layout (location = 2) in float aNormal;
layout (location = 3) in float aSunlight;
layout (location = 4) in float aBlockLight;
layout (location = 5) in float aAO;
layout (location = 6) in float aLayer;
layout (location = 7) in float aAnimationFrameCount;
layout (location = 8) in float aAnimationFps;
layout (location = 9) in float aAnimated;
layout (location = 10) in uint aTintPacked;

uniform mat4 viewProj;
uniform mat4 uShadowModelView;
uniform mat4 uShadowProjection;
uniform mat4 uShadowProjectionInverse;
uniform mat4 model;
uniform int uUseModel;
uniform int uShadowWarpMode;

out vec2 vUV;
out float vLayer;
out float vAnimationFrameCount;
out float vAnimationFps;
out float vAnimated;
out float vNormal;
out vec3 vWorldPos;
flat out int vMaterialKind;

float calculateShadowDistortion(vec2 coord) {
    if (uShadowWarpMode == 2) {
        return 1.0;
    }
    if (uShadowWarpMode == 1) {
        vec2 scaled = coord * 1.165;
        float quarticLength = pow(dot(scaled * scaled, scaled * scaled), 0.25);
        return quarticLength * 0.9 + 0.1;
    }
    return length(coord * 1.169) * 0.9 + 0.1;
}

void main() {
    vec4 localPos = vec4(aPos, 1.0);
    vec4 worldPos = (uUseModel != 0) ? model * localPos : localPos;
    vec4 clipPos = uShadowProjection * (uShadowModelView * worldPos);
    if (uShadowWarpMode != 2) {
        clipPos.xy /= calculateShadowDistortion(clipPos.xy);
        clipPos.z *= 0.2;
    }
    gl_Position = clipPos;
    vUV = aUV;
    vLayer = aLayer;
    vAnimationFrameCount = aAnimationFrameCount;
    vAnimationFps = aAnimationFps;
    vAnimated = aAnimated;
    vNormal = aNormal;
    vWorldPos = worldPos.xyz;

    // Extract DerivativeMain material id from packed tint data.
    uint materialKind = (aTintPacked >> 8u) & 63u;
    vMaterialKind = int(materialKind);
}
