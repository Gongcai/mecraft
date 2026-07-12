#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aShade;
layout(location = 0) out vec2 vUv;
layout(location = 1) out float vShade;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uLighting;
};
void main() {
    gl_Position = uViewProj * uModel * vec4(aPosition, 1.0);
    vUv = aUv;
    vShade = aShade;
}
