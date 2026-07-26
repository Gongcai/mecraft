#version 450 core

#include "ssao_upsample_common.glsl"

layout(location = 0) in vec2 vScreenUv;
layout(location = 1) in vec2 vClipUv;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = renderSsaoUpsample(vScreenUv, ivec2(gl_FragCoord.xy));
}
