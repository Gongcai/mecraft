#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in int aNormal;
layout(location = 3) in float aSunlight;
layout(location = 4) in float aBlockLight;
layout(location = 5) in uint aAo;
layout(location = 6) in uint aLayer;
layout(location = 7) in uint aAnimationFrameCount;
layout(location = 8) in uint aAnimationFps;
layout(location = 9) in uint aAnimated;
layout(location = 10) in uint aTintPacked;
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vAnimationData;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uAnimationTime;
};
void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vUv = aUv;
    vAnimationData = vec4(float(aLayer), float(aAnimationFrameCount),
                          float(aAnimationFps), float(aAnimated));
}
