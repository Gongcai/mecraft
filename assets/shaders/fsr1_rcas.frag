#version 450 core

#define A_GPU 1
#define A_GLSL 1
#include "ffx-fsr/ffx_a.h"

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

layout(binding = 0) uniform sampler2D uInputTex;

layout(push_constant) uniform RhiPushConstants {
    vec4 uCon;
};

#define FSR_RCAS_F 1
AF4 FsrRcasLoadF(ASU2 p) {
    ivec2 size = textureSize(uInputTex, 0);
    ivec2 texel = clamp(ivec2(p), ivec2(0), size - ivec2(1));
    return texelFetch(uInputTex, texel, 0);
}

void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {
}

#include "ffx-fsr/ffx_fsr1.h"

void main() {
    AF1 r;
    AF1 g;
    AF1 b;
    FsrRcasF(r, g, b, AU2(gl_FragCoord.xy), floatBitsToUint(uCon));
    FragColor = vec4(clamp(vec3(r, g, b), vec3(0.0), vec3(1.0)), 1.0);
}
