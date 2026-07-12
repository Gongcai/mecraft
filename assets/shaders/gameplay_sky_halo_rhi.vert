#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 0) out vec4 vColor;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uTint;
};
void main() {
    vColor = aColor;
    vec4 position = uViewProj * uModel * vec4(aPosition, 1.0);
    gl_Position = position.xyww;
}
