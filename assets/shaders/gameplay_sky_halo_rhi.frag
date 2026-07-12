#version 450 core
layout(location = 0) in vec4 vColor;
layout(location = 0) out vec4 fragColor;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uTint;
};
vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}
void main() {
    fragColor = vec4(srgbToLinear(vColor.rgb) * srgbToLinear(uTint.rgb),
                     vColor.a * uTint.a);
}
