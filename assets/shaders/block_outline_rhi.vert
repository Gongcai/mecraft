#version 450 core
layout(location = 0) in vec3 aPosition;
layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uColor;
};
void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
