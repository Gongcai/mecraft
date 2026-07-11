#version 450 core
layout(location = 0) in float vShade;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 15) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uTint;
};
vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}
void main() {
    fragColor = vec4(srgbToLinear(uTint.rgb) * vShade, uTint.a);
}
