#version 450

layout(set = 0, binding = 0) uniform sampler2D uFontAtlas;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main() {
    float coverage = texture(uFontAtlas, vUv).r;
    if (coverage <= 0.001) {
        discard;
    }
    outColor = vec4(vColor.rgb, vColor.a * coverage);
}
