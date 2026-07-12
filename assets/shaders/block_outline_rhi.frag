#version 450 core
layout(location = 0) out vec4 fragColor;
layout(push_constant) uniform RhiPushConstants {
    mat4 uViewProj;
    mat4 uModel;
    vec4 uColor;
};
void main() {
    fragColor = vec4(pow(max(uColor.rgb, vec3(0.0)), vec3(2.2)), uColor.a);
}
