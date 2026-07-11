#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec3 vLocalPos;
layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uBlockPosProgress;
    ivec4 uFlags;
};
void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vUV = aUv;
    vWorldPos = worldPos.xyz;
    vLocalPos = aPosition;
    gl_Position = uViewProj * worldPos;
}
