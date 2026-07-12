#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 5) in uint aAo;
layout(location = 6) in uint aLayer;
layout(location = 10) in uint aTintPacked;
layout(location = 0) out vec2 vUv;
layout(location = 1) out float vAo;
layout(location = 2) flat out uint vLayer;
layout(location = 3) flat out uint vTintKind;
layout(location = 4) out vec2 vTintUv;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLighting;
};
void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vUv = aUv;
    vAo = float(aAo);
    vLayer = aLayer;
    vTintKind = (aTintPacked >> 14u) & 3u;
    vTintUv = (vec2(float((aTintPacked >> 4u) & 15u),
                    float(aTintPacked & 15u)) + 0.5) / 16.0;
}
