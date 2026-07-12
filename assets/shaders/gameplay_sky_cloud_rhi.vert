#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aShade;
layout(location = 0) out float vShade;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uTint;
};
void main() {
    vShade = aShade;
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
}
